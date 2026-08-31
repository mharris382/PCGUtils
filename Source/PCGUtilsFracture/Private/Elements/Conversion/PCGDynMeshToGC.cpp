// Copyright Max Harris

#include "Elements/Conversion/PCGDynMeshToGC.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"
#include "VertexConnectedComponents.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshToGC"

namespace
{
	using namespace UE::Geometry;

	/** Adds an empty cluster root and returns its transform index. Mirrors GeometryCollectionMeshNodes.cpp. */
	int32 AddClusterRoot(FGeometryCollection& InOutCollection)
	{
		const int32 Index = InOutCollection.AddElements(1, FGeometryCollection::TransformGroup);
		InOutCollection.Parent[Index] = INDEX_NONE;
		InOutCollection.BoneColor[Index] = FLinearColor::White;
		return Index;
	}

	/**
	 * Splits one mesh into connected components when requested. Lifted from
	 * FMeshToCollectionDataflowNode_v2::Evaluate so the behaviour matches Epic's own node exactly.
	 */
	void SplitIntoIslands(
		const FDynamicMesh3& InMesh,
		const UPCGDynMeshToGCSettings* Settings,
		TArray<TUniquePtr<FDynamicMesh3>>& OutMeshes)
	{
		FVertexConnectedComponents Components(InMesh.MaxVertexID());
		Components.ConnectTriangles(InMesh);
		if (Settings->bConnectIslandsByVertexOverlap)
		{
			Components.ConnectCloseVertices(InMesh, Settings->ConnectVerticesThreshold, 2);
		}
		if (Settings->VertexToSurfaceBridgeDistance > 0.0f)
		{
			TMeshAABBTree3<FDynamicMesh3> Spatial(&InMesh);
			Components.ConnectVerticesToNearestDifferentComponent(
				Spatial, static_cast<double>(Settings->VertexToSurfaceBridgeDistance));
		}
		FDynamicMeshEditor::SplitMesh(&InMesh, OutMeshes, false, [&Components, &InMesh](int32 TID)
		{
			return Components.GetComponent(InMesh.GetTriangle(TID).A);
		});
	}

	/**
	 * Offsets every MaterialID on a mesh so it indexes into a combined material array. Needed because each
	 * input DynMesh numbers its own materials from zero, and the collection stores only integer IDs.
	 */
	void OffsetMaterialIDs(FDynamicMesh3& InOutMesh, int32 InOffset)
	{
		if (InOffset == 0 || !InOutMesh.HasAttributes())
		{
			return;
		}
		FDynamicMeshMaterialAttribute* MaterialIDs = InOutMesh.Attributes()->GetMaterialID();
		if (!MaterialIDs)
		{
			return;
		}
		for (const int32 TID : InOutMesh.TriangleIndicesItr())
		{
			const int32 MaterialID = MaterialIDs->GetValue(TID);
			MaterialIDs->SetValue(TID, FMath::Max(0, MaterialID) + InOffset);
		}
	}

	/** Appends every prepared mesh under a fresh cluster root, returning the number of geometry bones added. */
	int32 BuildCollection(
		FGeometryCollection& OutCollection,
		const TArray<TUniquePtr<FDynamicMesh3>>& InMeshes)
	{
		FGeometryCollectionToDynamicMeshes Convert;
		FGeometryCollectionToDynamicMeshes::FToCollectionOptions Options;

		// D3: always parent geometry under an explicit cluster root. FFractureEngineEdit::DeleteBranch calls
		// RemoveRootNodes and will never delete a root bone, so a geometry bone that *is* the root would be
		// silently unprunable. Costs one transform element and makes every graph behave the same way.
		Options.NewMeshParentIndex = AddClusterRoot(OutCollection);
		Options.bAllowAppendAsRoot = false;

		int32 NumAdded = 0;
		for (const TUniquePtr<FDynamicMesh3>& Mesh : InMeshes)
		{
			if (!Mesh || Mesh->TriangleCount() == 0)
			{
				continue;
			}
			// Identity transform: this is the whole transform-space policy. Vertices land in the collection
			// verbatim, the bone transform is identity, and nothing is re-pivoted anywhere in the round trip.
			const int32 TransformIndex =
				FGeometryCollectionToDynamicMeshes::AppendMeshToCollection(
					OutCollection, *Mesh, FTransform::Identity, Options);
			if (TransformIndex != INDEX_NONE)
			{
				++NumAdded;
			}
		}
		return NumAdded;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshToGCSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh To GC");
}

FText UPCGDynMeshToGCSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Converts DynMesh data into a transient Geometry Collection so Unreal's fracture backend can be used as "
		"a procedural modelling step. Geometry is placed under a cluster root at identity, preserving the "
		"DynMesh's own local space with no re-pivoting. No asset, actor or component is created.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshToGCSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshToGCConstants::MeshInputPin,
		EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshToGCSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGDynMeshToGCConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true));
	return Pins;
}

FPCGElementPtr UPCGDynMeshToGCSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshToGCElement>();
}

bool FPCGDynMeshToGCElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace UE::Geometry;

	check(Context);
	const UPCGDynMeshToGCSettings* Settings = Context->GetInputSettings<UPCGDynMeshToGCSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs =
		Context->InputData.GetInputsByPin(PCGDynMeshToGCConstants::MeshInputPin);

	// Prepared meshes plus the combined material array they were remapped against. When merging, one batch;
	// otherwise one batch per input.
	struct FBatch
	{
		TArray<TUniquePtr<FDynamicMesh3>> Meshes;
		TArray<TObjectPtr<UMaterialInterface>> Materials;
		const FPCGTaggedData* SourceInput = nullptr;
	};

	TArray<FBatch> Batches;
	if (Settings->bMergeInputsIntoOneCollection)
	{
		Batches.Emplace();
	}

	int32 NumSkippedInputs = 0;
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const FDynamicMesh3* SourceMesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!SourceMesh || SourceMesh->TriangleCount() == 0)
		{
			++NumSkippedInputs;
			continue;
		}

		FBatch& Batch = Settings->bMergeInputsIntoOneCollection ? Batches[0] : Batches.Emplace_GetRef();
		if (!Settings->bMergeInputsIntoOneCollection)
		{
			Batch.SourceInput = &Input;
		}

		// Material IDs are per-mesh; shift them so they index the batch's combined array.
		const int32 MaterialOffset = Batch.Materials.Num();
		Batch.Materials.Append(MeshData->GetMaterials());

		TArray<TUniquePtr<FDynamicMesh3>> Prepared;
		if (Settings->bSplitIslands)
		{
			SplitIntoIslands(*SourceMesh, Settings, Prepared);
		}
		if (Prepared.IsEmpty())
		{
			Prepared.Emplace(MakeUnique<FDynamicMesh3>(*SourceMesh));
		}

		for (TUniquePtr<FDynamicMesh3>& Mesh : Prepared)
		{
			OffsetMaterialIDs(*Mesh, MaterialOffset);
			Batch.Meshes.Emplace(MoveTemp(Mesh));
		}
	}

	if (NumSkippedInputs > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("SkippedEmptyMeshes", "DynMesh To GC skipped {0} input(s) with no valid mesh geometry."),
			FText::AsNumber(NumSkippedInputs)), Context);
	}

	bool bProducedAnything = false;
	for (FBatch& Batch : Batches)
	{
		if (Batch.Meshes.IsEmpty())
		{
			continue;
		}

		TSharedRef<FGeometryCollection> Collection = MakeShared<FGeometryCollection>();
		const int32 NumBones = BuildCollection(*Collection, Batch.Meshes);
		if (NumBones == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ConversionFailed", "DynMesh To GC produced no geometry bones."), Context);
			continue;
		}

		// Sections drive material batching for any later conversion; build them once here.
		Collection->ReindexMaterials();

		// A collection that leaves this node must be fracture-ready. The fracture backend guards on these
		// attributes silently and just returns INDEX_NONE, so catching it at the producer is the difference
		// between naming the missing attribute and leaving the user to guess at the far end of the graph.
		TArray<FString> MissingAttributes;
		if (!PCGUtilsGCHelpers::ValidateFractureRequirements(*Collection, MissingAttributes))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MalformedOutput",
					"DynMesh To GC produced a Geometry Collection missing required attribute(s): {0}. This is a "
					"bug - please report it with the source mesh."),
				FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), Context);
			continue;
		}

		UPCGGeometryCollectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(Context);
		OutputData->Initialize(Collection, MoveTemp(Batch.Materials));

		FPCGTaggedData& Output = Batch.SourceInput
			? Context->OutputData.TaggedData.Emplace_GetRef(*Batch.SourceInput)
			: Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGDynMeshToGCConstants::CollectionOutputPin;
		bProducedAnything = true;

		UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("DynMesh To GC: %d input mesh(es) -> %s"),
			Batch.Meshes.Num(), *PCGUtilsGCHelpers::DescribeCollection(*Collection));
	}

	if (!bProducedAnything && !Inputs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoOutput", "DynMesh To GC produced no Geometry Collection from its inputs."), Context);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
