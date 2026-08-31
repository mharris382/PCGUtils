// Copyright Max Harris

#include "Elements/Conversion/PCGGeometryCollectionToDynMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCToDynMesh"

namespace
{
	using namespace UE::Geometry;

	/** Finds an extended PolyGroup layer by name, adding one if it is not already present. */
	FDynamicMeshPolygroupAttribute* FindOrAddPolygroupLayer(FDynamicMesh3& InOutMesh, FName InLayerName)
	{
		if (!InOutMesh.HasAttributes())
		{
			InOutMesh.EnableAttributes();
		}
		FDynamicMeshAttributeSet* Attributes = InOutMesh.Attributes();

		for (int32 Index = 0; Index < Attributes->NumPolygroupLayers(); ++Index)
		{
			if (Attributes->GetPolygroupLayer(Index)->GetName() == InLayerName)
			{
				return Attributes->GetPolygroupLayer(Index);
			}
		}

		const int32 NewIndex = Attributes->NumPolygroupLayers();
		Attributes->SetNumPolygroupLayers(NewIndex + 1);
		FDynamicMeshPolygroupAttribute* Layer = Attributes->GetPolygroupLayer(NewIndex);
		Layer->SetName(InLayerName);
		return Layer;
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionToDynMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC To DynMesh");
}

FText UPCGGeometryCollectionToDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Combines the surviving Geometry Collection pieces into one DynMesh, in the collection's own local "
		"space with no re-pivoting. Writes a PolyGroup layer per bone and keeps the engine's interior/exterior "
		"face tagging, so fracture pieces and fracture-generated interior surfaces stay selectable via Select "
		"by PolyGroup.");
}
#endif

TArray<FPCGPinProperties> UPCGGeometryCollectionToDynMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGeometryCollectionToDynMeshConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionToDynMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGeometryCollectionToDynMeshConstants::MeshOutputPin, EPCGDataType::DynamicMesh, true, true));
	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionToDynMeshSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionToDynMeshElement>();
}

bool FPCGGeometryCollectionToDynMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace UE::Geometry;

	check(Context);
	const UPCGGeometryCollectionToDynMeshSettings* Settings = Context->GetInputSettings<UPCGGeometryCollectionToDynMeshSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGeometryCollectionToDynMeshConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* CollectionData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!CollectionData || !CollectionData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "GC To DynMesh skipped an input with no valid Geometry Collection."),
				Context);
			continue;
		}

		const FGeometryCollection& Collection = CollectionData->GetCollection();

		TArray<int32> GeometryBones;
		PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Collection, GeometryBones);
		if (GeometryBones.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "GC To DynMesh received a collection with no geometry-bearing bones."),
				Context);
			continue;
		}

		FGeometryCollectionToDynamicMeshes CollectionToMeshes;
		FGeometryCollectionToDynamicMeshes::FToMeshOptions ToMeshOptions;
		// Identity: the collection is already in the source DynMesh's local space and must not be re-pivoted.
		ToMeshOptions.Transform = FTransform::Identity;
		ToMeshOptions.bWeldVertices = Settings->bWeldVertices;
		ToMeshOptions.bSaveIsolatedVertices = Settings->bPreserveIsolatedVertices;
		// Engine default. Produces the named layer "GeometryCollectionInternalFaces" distinguishing
		// fracture-generated interior surfaces from the original exterior ones.
		ToMeshOptions.bInternalFaceTagsAsPolygroups = Settings->bTagInternalFaces;
		ToMeshOptions.InvisibleFaces = FGeometryCollectionToDynamicMeshes::EInvisibleFaceConversion::Skip;

		if (!CollectionToMeshes.InitFromTransformSelection(Collection, GeometryBones, ToMeshOptions)
			|| CollectionToMeshes.Meshes.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ConversionFailed", "GC To DynMesh could not convert the Geometry Collection."), Context);
			continue;
		}

		FDynamicMesh3 CombinedMesh;
		// Per-bone triangle ranges, recorded during the append so the PolyGroup pass can attribute every
		// triangle to the bone it came from. AppendWithOffsets reports the offset and count for each append.
		struct FBoneRange { int32 TransformIndex; int32 TriangleStart; int32 TriangleEnd; };
		TArray<FBoneRange> BoneRanges;
		BoneRanges.Reserve(CollectionToMeshes.Meshes.Num());

		for (int32 MeshIdx = 0; MeshIdx < CollectionToMeshes.Meshes.Num(); ++MeshIdx)
		{
			FDynamicMesh3& SourceMesh = *CollectionToMeshes.Meshes[MeshIdx].Mesh;
			const int32 TransformIndex = CollectionToMeshes.Meshes[MeshIdx].TransformIndex;

			if (MeshIdx == 0)
			{
				const int32 TriangleEnd = SourceMesh.MaxTriangleID();
				CombinedMesh = MoveTemp(SourceMesh);
				BoneRanges.Add({TransformIndex, 0, TriangleEnd});
				continue;
			}

			// AppendWithOffsets only carries attributes the destination already has, so match the layouts
			// first - otherwise the per-bone and internal-face PolyGroup layers silently do not survive.
			CombinedMesh.EnableMatchingAttributes(SourceMesh, /*bClearExisting=*/false,
				/*bDiscardExtraAttributes=*/false);

			FDynamicMesh3::FAppendInfo AppendInfo;
			CombinedMesh.AppendWithOffsets(SourceMesh, &AppendInfo);
			BoneRanges.Add({
				TransformIndex,
				AppendInfo.TriangleOffset,
				AppendInfo.TriangleOffset + AppendInfo.NumTriangle});
		}

		if (Settings->bSetPolygroupPerBone && !Settings->BonePolygroupLayerName.IsNone())
		{
			FDynamicMeshPolygroupAttribute* BoneLayer =
				FindOrAddPolygroupLayer(CombinedMesh, Settings->BonePolygroupLayerName);
			if (BoneLayer)
			{
				for (const FBoneRange& Range : BoneRanges)
				{
					for (int32 TID = Range.TriangleStart; TID < Range.TriangleEnd; ++TID)
					{
						if (CombinedMesh.IsTriangle(TID))
						{
							BoneLayer->SetValue(TID, Range.TransformIndex);
						}
					}
				}
			}
			else
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("BoneLayerFailed", "GC To DynMesh could not create the PolyGroup layer '{0}'."),
					FText::FromName(Settings->BonePolygroupLayerName)), Context);
			}
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(CollectionData->GetMaterials().Num());
		for (const TObjectPtr<UMaterialInterface>& Material : CollectionData->GetMaterials())
		{
			Materials.Add(Material);
		}

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(CombinedMesh), Materials);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionToDynMeshConstants::MeshOutputPin;

		const UE::Geometry::FDynamicMesh3* ResultMesh =
			OutputData->GetDynamicMesh() ? OutputData->GetDynamicMesh()->GetMeshPtr() : nullptr;
		UE_LOG(LogPCGUtilsFracture, Verbose,
			TEXT("GC To DynMesh: %d bone(s) -> vertices: %d, triangles: %d"),
			BoneRanges.Num(),
			ResultMesh ? ResultMesh->VertexCount() : 0,
			ResultMesh ? ResultMesh->TriangleCount() : 0);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
