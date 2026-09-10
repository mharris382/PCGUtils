// Copyright Max Harris

#include "Elements/Conversion/PCGGeometryCollectionToDynMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionMeshPresentation.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCToDynMesh"

namespace
{
	using namespace UE::Geometry;
	namespace Presentation = PCGUtilsGeometryCollectionMeshPresentation;

	/** Removes a named PolyGroup layer, for the layers the user asked not to have. */
	void RemovePolygroupLayer(FDynamicMesh3& InOutMesh, FName InLayerName)
	{
		if (!InOutMesh.HasAttributes())
		{
			return;
		}

		FDynamicMeshAttributeSet* Attributes = InOutMesh.Attributes();
		for (int32 Index = Attributes->NumPolygroupLayers() - 1; Index >= 0; --Index)
		{
			if (Attributes->GetPolygroupLayer(Index)->GetName() == InLayerName)
			{
				// SetNumPolygroupLayers only truncates, so a layer that is not last is swapped to the end
				// first. Order carries no meaning - every consumer resolves layers by name.
				const int32 LastIndex = Attributes->NumPolygroupLayers() - 1;
				if (Index != LastIndex)
				{
					FDynamicMeshPolygroupAttribute* Layer = Attributes->GetPolygroupLayer(Index);
					const FDynamicMeshPolygroupAttribute* LastLayer = Attributes->GetPolygroupLayer(LastIndex);
					const FName LastName = LastLayer->GetName();
					for (const int32 TriangleID : InOutMesh.TriangleIndicesItr())
					{
						Layer->SetValue(TriangleID, LastLayer->GetValue(TriangleID));
					}
					Layer->SetName(LastName);
				}
				Attributes->SetNumPolygroupLayers(LastIndex);
				return;
			}
		}
	}

	/** Strips the layers the settings did not ask for, after everything that needed them has run. */
	void ApplyLayerSettings(
		FDynamicMesh3& InOutMesh, const UPCGGeometryCollectionToDynMeshSettings* InSettings)
	{
		if (!InSettings->bTagInternalFaces)
		{
			RemovePolygroupLayer(
				InOutMesh, PCGUtilsGeometryCollectionPieceMesh::InternalFacePolygroupLayerName());
		}

		if (!InSettings->bIncludeHiddenFaces)
		{
			// Every remaining face is visible, so the layer would say nothing. Kept when hidden faces were
			// emitted, because then it is the only way to tell them apart.
			RemovePolygroupLayer(
				InOutMesh, PCGUtilsGeometryCollectionPieceMesh::VisibleFacePolygroupLayerName());
		}
	}

	/** Writes one piece's identity onto its output data's data domain. */
	void WritePieceAttributes(
		UPCGDynamicMeshData* InOutData,
		const UPCGGeometryCollectionToDynMeshSettings* InSettings,
		const UPCGGeometryCollectionData* InCollectionData,
		int32 InBoneIndex,
		int32 InGeometryIndex,
		FPCGContext* InContext)
	{
		UPCGMetadata* Metadata = InOutData->MutableMetadata();
		FPCGMetadataDomain* DataDomain =
			Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!DataDomain)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoDataDomain", "GC To DynMesh could not write per-piece attributes."), InContext);
			return;
		}

		// The data domain holds one entry, standing for the data itself.
		if (DataDomain->GetItemCountForChild() == 0)
		{
			DataDomain->AddEntry();
		}

		bool bAllNamed = true;
		auto Write = [DataDomain, InContext, &bAllNamed]<typename ValueType>(
			bool bEnabled, FName Name, ValueType Value, const TCHAR* Label)
		{
			if (!bEnabled)
			{
				return;
			}
			if (Name.IsNone())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("UnnamedAttribute",
						"GC To DynMesh has {0} enabled but its attribute name is empty."),
					FText::FromString(Label)), InContext);
				bAllNamed = false;
				return;
			}
			if (FPCGMetadataAttribute<ValueType>* Attribute =
				DataDomain->FindOrCreateAttribute<ValueType>(Name, Value, false, true))
			{
				Attribute->SetValue(PCGFirstEntryKey, Value);
			}
		};

		const FGeometryCollection& Collection = InCollectionData->GetCollection();

		// Identity is unconditional: it is the contract that lets a downstream selection be resolved against
		// the collection this piece came from.
		Write(true, InSettings->BoneIndexAttributeName, InBoneIndex, TEXT("Bone Index"));
		Write(true, InSettings->SourceIdAttributeName,
			PCGUtilsGeometryCollectionIdentity::FoldGuid(InCollectionData->GetCollectionId()), TEXT("Source Id"));
		Write(true, InSettings->SourceRevisionAttributeName,
			InCollectionData->GetRevision(), TEXT("Source Revision"));
		Write(true, InSettings->SourceStateIdAttributeName,
			PCGUtilsGeometryCollectionIdentity::FoldGuid(InCollectionData->GetStateId()), TEXT("Source State Id"));

		Write(InSettings->bOutputGeometryIndex, InSettings->GeometryIndexAttributeName,
			InGeometryIndex, TEXT("Geometry Index"));
		Write(InSettings->bOutputParentIndex, InSettings->ParentIndexAttributeName,
			PCGUtilsGeometryCollectionHierarchy::GetParent(Collection, InBoneIndex), TEXT("Parent Index"));
		Write(InSettings->bOutputHierarchyLevel, InSettings->HierarchyLevelAttributeName,
			PCGUtilsGeometryCollectionHierarchy::GetLevel(Collection, InBoneIndex), TEXT("Hierarchy Level"));

		if (InSettings->NeedsSurfaceInfo())
		{
			// Measured from the collection's own per-face flags rather than from the emitted mesh, so the
			// value describes the piece and matches what GC Bones To Points reports for the same bone.
			TArray<FTransform> GlobalTransforms;
			PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);
			const FTransform BoneToCollection = GlobalTransforms.IsValidIndex(InBoneIndex)
				? GlobalTransforms[InBoneIndex] : FTransform::Identity;

			const PCGUtilsGeometryCollectionHelpers::FBoneSurfaceInfo Surface =
				PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(Collection, InBoneIndex, BoneToCollection);

			Write(InSettings->bOutputIsExterior, InSettings->IsExteriorAttributeName,
				Surface.IsExterior(), TEXT("Is Exterior"));
			Write(InSettings->bOutputExposureRatio, InSettings->ExposureRatioAttributeName,
				Surface.ExposureRatio(), TEXT("Exposure Ratio"));
		}
	}

	TArray<UMaterialInterface*> ResolveMaterials(const UPCGGeometryCollectionData* InCollectionData)
	{
		// Face MaterialIDs index the collection's array absolutely, so every output carries the whole array
		// rather than a per-piece subset - remapping would invalidate the ids the pieces already hold.
		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(InCollectionData->GetMaterials().Num());
		for (const TObjectPtr<UMaterialInterface>& Material : InCollectionData->GetMaterials())
		{
			Materials.Add(Material);
		}
		return Materials;
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionToDynMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC|To DynMesh");
}

FText UPCGGeometryCollectionToDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Converts the surviving Geometry Collection pieces back to DynMesh, in the collection's own local "
		"space with no re-pivoting. Combined appends every piece into one mesh - the round-trip form, a solid "
		"with a real cavity in it. Per Piece emits one mesh per fracture piece, each carrying the identity of "
		"the bone it came from. Either way a PolyGroup layer per bone and the interior/exterior face tagging "
		"survive, so pieces and cut surfaces stay selectable via Select by PolyGroup.");
}

FString UPCGGeometryCollectionToDynMeshSettings::GetAdditionalTitleInformation() const
{
	return OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece ? TEXT("Per Piece") : FString();
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

		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
		if (Pieces.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "GC To DynMesh received a collection with no fracture pieces."),
				Context);
			continue;
		}

		// Bone transforms are parent-relative, so collection space needs the global matrices. They are
		// identity throughout this module's own round trip, which is why presenting is usually free.
		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);

		Presentation::FPresentationOptions PresentationOptions;
		PresentationOptions.bSkipHiddenFaces = !Settings->bIncludeHiddenFaces;
		PresentationOptions.bWeldVertices = Settings->bWeldVertices;
		PresentationOptions.bPreserveIsolatedVertices = Settings->bPreserveIsolatedVertices;

		const bool bPerPiece = Settings->OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece;
		const bool bPieceLocal =
			bPerPiece && Settings->Space == EPCGGeometryCollectionToDynMeshSpace::PieceLocal;

		TArray<FDynamicMesh3> PresentedMeshes;
		TArray<Presentation::FCombinedPieceRange> Identities;
		PresentedMeshes.SetNum(Pieces.Num());
		Identities.Reserve(Pieces.Num());

		int32 NumConverted = 0;
		for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		{
			const int32 BoneIndex = Pieces[Index];
			const int32 GeometryIndex = Collection.TransformToGeometryIndex[BoneIndex];

			// The shared, lazily-built canonical view: every consumer of this collection state - this node,
			// a selector, a later per-piece export - converts each piece at most once between them.
			const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
				CollectionData->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndex);
			if (!View.IsValid())
			{
				continue;
			}

			const FTransform BoneToTarget = bPieceLocal
				? FTransform::Identity
				: (GlobalTransforms.IsValidIndex(BoneIndex) ? GlobalTransforms[BoneIndex] : FTransform::Identity);

			Presentation::PresentPiece(*View, BoneToTarget, PresentationOptions, PresentedMeshes[Index]);

			Presentation::FCombinedPieceRange Identity;
			Identity.TransformIndex = BoneIndex;
			Identity.GeometryIndex = GeometryIndex;
			Identities.Add(Identity);
			++NumConverted;
		}

		if (NumConverted == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ConversionFailed", "GC To DynMesh could not convert the Geometry Collection."), Context);
			continue;
		}

		const TArray<UMaterialInterface*> Materials = ResolveMaterials(CollectionData);

		if (bPerPiece)
		{
			int32 NumEmitted = 0;
			for (int32 Index = 0; Index < Identities.Num(); ++Index)
			{
				FDynamicMesh3& PieceMesh = PresentedMeshes[Index];
				if (PieceMesh.TriangleCount() == 0)
				{
					continue;
				}

				// One range covering the whole mesh: the layer is redundant with one piece per output, but
				// keeping it means a later Combine or Merge does not lose which bone a triangle came from.
				if (Settings->bSetPolygroupPerBone)
				{
					Presentation::WriteBonePolygroupLayer(
						PieceMesh, Settings->BonePolygroupLayerName,
						{Presentation::FCombinedPieceRange{
							Identities[Index].TransformIndex, Identities[Index].GeometryIndex,
							0, PieceMesh.MaxTriangleID()}});
				}
				ApplyLayerSettings(PieceMesh, Settings);

				UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
				OutputData->Initialize(MoveTemp(PieceMesh), Materials);

				WritePieceAttributes(
					OutputData, Settings, CollectionData,
					Identities[Index].TransformIndex, Identities[Index].GeometryIndex, Context);

				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				Output.Data = OutputData;
				Output.Pin = PCGGeometryCollectionToDynMeshConstants::MeshOutputPin;
				++NumEmitted;
			}

			UE_LOG(LogPCGUtilsFracture, Verbose,
				TEXT("GC To DynMesh: %d piece(s) -> %d mesh(es), space: %s"),
				NumConverted, NumEmitted, bPieceLocal ? TEXT("piece local") : TEXT("collection"));
			continue;
		}

		TArray<const FDynamicMesh3*> PresentedPointers;
		PresentedPointers.Reserve(Identities.Num());
		for (int32 Index = 0; Index < Identities.Num(); ++Index)
		{
			PresentedPointers.Add(&PresentedMeshes[Index]);
		}

		FDynamicMesh3 CombinedMesh;
		TArray<Presentation::FCombinedPieceRange> Ranges;
		Presentation::CombinePieces(PresentedPointers, Identities, CombinedMesh, Ranges);

		if (Settings->bSetPolygroupPerBone
			&& !Presentation::WriteBonePolygroupLayer(CombinedMesh, Settings->BonePolygroupLayerName, Ranges))
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("BoneLayerFailed", "GC To DynMesh could not create the PolyGroup layer '{0}'."),
				FText::FromName(Settings->BonePolygroupLayerName)), Context);
		}
		ApplyLayerSettings(CombinedMesh, Settings);

		const int32 CombinedTriangles = CombinedMesh.TriangleCount();
		const int32 CombinedVertices = CombinedMesh.VertexCount();

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(CombinedMesh), Materials);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionToDynMeshConstants::MeshOutputPin;

		UE_LOG(LogPCGUtilsFracture, Verbose,
			TEXT("GC To DynMesh: %d piece(s) -> vertices: %d, triangles: %d"),
			NumConverted, CombinedVertices, CombinedTriangles);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
