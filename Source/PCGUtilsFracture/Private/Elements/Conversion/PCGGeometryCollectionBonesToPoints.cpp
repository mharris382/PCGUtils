// Copyright Max Harris

#include "Elements/Conversion/PCGGeometryCollectionBonesToPoints.h"

#include "Data/PCGUtilsClusterInterop.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionBonePoints.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCBonesToPoints"


namespace
{
	/**
	 * Writes the bone adjacency graph as a PCGEx-compatible cluster: marks the vtx data and emits the matching
	 * edges data on its own pin.
	 *
	 * Nothing here links against PCGExtendedToolkit - the contract is two int64 attributes and three tags, so
	 * reproducing it is cheaper and less brittle than a dependency in either direction.
	 *
	 * @return number of edges emitted.
	 */
	int32 EmitCluster(
		FPCGContext* Context,
		const UPCGGeometryCollectionBonesToPointsSettings* Settings,
		const FGeometryCollection& Collection,
		const TArray<int32>& Bones,
		const FTransform& LocalToWorld,
		const TArray<FTransform>& GlobalTransforms,
		UPCGPointArrayData* VtxData,
		FPCGTaggedData& VtxTaggedData)
	{
		using namespace PCGUtilsClusterInterop;

		TArray<PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge> AdjacencyEdges;
		if (!PCGUtilsGeometryCollectionHelpers::BuildBoneAdjacency(
			Collection, Settings->NeedsContactInfo(), AdjacencyEdges))
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoProximity",
					"GC Bones To Points could not determine which pieces touch, so no cluster edges were "
					"emitted. The vertices are still usable on their own."), Context);
		}

		// Only bones that made it into the points output can be cluster vertices; an edge to a bone that was
		// filtered out (cluster bones excluded, say) would dangle.
		TSet<int32> EmittedBones(Bones);
		TMap<int32, int32> DegreeByBone;
		TArray<PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge> UsableEdges;
		UsableEdges.Reserve(AdjacencyEdges.Num());
		for (const PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge& Edge : AdjacencyEdges)
		{
			if (!EmittedBones.Contains(Edge.BoneA) || !EmittedBones.Contains(Edge.BoneB))
			{
				continue;
			}
			UsableEdges.Add(Edge);
			DegreeByBone.FindOrAdd(Edge.BoneA)++;
			DegreeByBone.FindOrAdd(Edge.BoneB)++;
		}

		// --- Vtx half -------------------------------------------------------------------------------------
		// The vertex id is the bone index rather than the point index. PCGEx's reader builds a
		// VtxId -> point-index map from this attribute, so any unique id is legal - and using the bone index
		// keeps it identical to GC_BoneIndex, so Select Bones From Points still works on the far side.
		if (FPCGMetadataDomain* VtxDomain =
			VtxData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements))
		{
			if (FPCGMetadataAttribute<int64>* VtxAttribute =
				VtxDomain->FindOrCreateAttribute<int64>(VtxDataAttribute, 0, false, true))
			{
				const auto VtxEntries = VtxData->GetConstMetadataEntryValueRange();
				for (int32 Index = 0; Index < Bones.Num() && Index < VtxEntries.Num(); ++Index)
				{
					const int32 BoneIndex = Bones[Index];
					const int32* Degree = DegreeByBone.Find(BoneIndex);
					VtxAttribute->SetValue(VtxEntries[Index], MakeVtxData(BoneIndex, Degree ? *Degree : 0));
				}
			}
		}

		// A pair id shared by both halves is what tells PCGEx these two datas belong together.
		const int64 PairId = static_cast<int64>(VtxData->GetUniqueID());
		VtxTaggedData.Tags.Add(VtxTag);
		VtxTaggedData.Tags.Add(MakeClusterPairTag(PairId));

		// --- Edges half -----------------------------------------------------------------------------------
		UPCGPointArrayData* EdgesData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		EdgesData->SetNumPoints(UsableEdges.Num(), /*bInitializeValues=*/false);

		auto EdgeTransforms = EdgesData->GetTransformValueRange();
		auto EdgeDensities = EdgesData->GetDensityValueRange();
		auto EdgeBoundsMin = EdgesData->GetBoundsMinValueRange();
		auto EdgeBoundsMax = EdgesData->GetBoundsMaxValueRange();

		auto BoneCentre = [&Collection, &GlobalTransforms, &LocalToWorld](int32 BoneIndex) -> FVector
		{
			const FTransform BoneToCollection = GlobalTransforms.IsValidIndex(BoneIndex)
				? GlobalTransforms[BoneIndex] : FTransform::Identity;
			const FBox LocalBounds =
				PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, BoneIndex);
			const FVector LocalCentre = LocalBounds.IsValid ? LocalBounds.GetCenter() : FVector::ZeroVector;
			return (BoneToCollection * LocalToWorld).TransformPosition(LocalCentre);
		};

		for (int32 Index = 0; Index < UsableEdges.Num(); ++Index)
		{
			const PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge& Edge = UsableEdges[Index];
			// Midway between the two pieces, so the edge reads correctly when the cluster is drawn.
			EdgeTransforms[Index] =
				FTransform((BoneCentre(Edge.BoneA) + BoneCentre(Edge.BoneB)) * 0.5);
			EdgeDensities[Index] = 1.0f;
			EdgeBoundsMin[Index] = FVector::ZeroVector;
			EdgeBoundsMax[Index] = FVector::ZeroVector;
		}

		if (FPCGMetadataDomain* EdgeDomain =
			EdgesData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements))
		{
			FPCGMetadataAttribute<int64>* EndpointsAttribute =
				EdgeDomain->FindOrCreateAttribute<int64>(EdgeDataAttribute, -1, false, true);

			FPCGMetadataAttribute<double>* ContactAreaAttribute =
				(Settings->bOutputContactArea && !Settings->ContactAreaAttributeName.IsNone())
					? EdgeDomain->FindOrCreateAttribute<double>(
						Settings->ContactAreaAttributeName, 0.0, false, true)
					: nullptr;
			FPCGMetadataAttribute<double>* SharpContactAttribute =
				(Settings->bOutputSharpContactWidth && !Settings->SharpContactWidthAttributeName.IsNone())
					? EdgeDomain->FindOrCreateAttribute<double>(
						Settings->SharpContactWidthAttributeName, 0.0, false, true)
					: nullptr;

			auto EdgeEntries = EdgesData->GetMetadataEntryValueRange();
			for (int32 Index = 0; Index < UsableEdges.Num(); ++Index)
			{
				const PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge& Edge = UsableEdges[Index];
				EdgeEntries[Index] = PCGInvalidEntryKey;
				EdgeDomain->InitializeOnSet(EdgeEntries[Index]);
				const PCGMetadataEntryKey Entry = EdgeEntries[Index];

				if (EndpointsAttribute)
				{
					EndpointsAttribute->SetValue(Entry, MakeEdgeData(Edge.BoneA, Edge.BoneB));
				}
				if (ContactAreaAttribute)
				{
					ContactAreaAttribute->SetValue(Entry, static_cast<double>(Edge.ContactArea));
				}
				if (SharpContactAttribute)
				{
					SharpContactAttribute->SetValue(Entry, static_cast<double>(Edge.SharpContactWidth));
				}
			}
		}

		FPCGTaggedData& EdgesOutput = Context->OutputData.TaggedData.Emplace_GetRef();
		EdgesOutput.Data = EdgesData;
		EdgesOutput.Pin = PCGGeometryCollectionBonesToPointsConstants::EdgesOutputPin;
		EdgesOutput.Tags.Add(EdgesTag);
		EdgesOutput.Tags.Add(MakeClusterPairTag(PairId));

		return UsableEdges.Num();
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionBonesToPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Bones To Points");
}

FText UPCGGeometryCollectionBonesToPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Outputs one point per geometry-bearing bone, positioned at the piece's centre with the piece's bounds "
		"and orientation. Each point carries GC_BoneIndex plus the source collection's identity, so a "
		"downstream Select Bones From Points can prove the selection still matches the collection it was "
		"authored against. Filter these points with ordinary PCG/PCGEx nodes.");
}
#endif

TArray<FPCGPinProperties> UPCGGeometryCollectionBonesToPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionBonesToPointsSettings::OutputPinProperties() const
{
	// Both halves are ordinary point pins. A cluster is point data marked with tags and attributes, not a
	// distinct data type - narrowing the pin would stop every other point node from accepting the output for
	// no benefit. PCGEx declares its own cluster pins the same way.
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGeometryCollectionBonesToPointsConstants::PointsOutputPin, EPCGDataType::Point, true, true));

	if (bOutputCluster)
	{
		Pins.Add(FPCGPinProperties(
			PCGGeometryCollectionBonesToPointsConstants::EdgesOutputPin, EPCGDataType::Point, true, true));
	}

	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionBonesToPointsSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionBonesToPointsElement>();
}

FPCGUtilsGeometryCollectionBonePointAttributes
UPCGGeometryCollectionBonesToPointsSettings::GetBonePointAttributes() const
{
	FPCGUtilsGeometryCollectionBonePointAttributes Attributes;

	Attributes.BoneIndexAttributeName = BoneIndexAttributeName;
	Attributes.SourceIdAttributeName = SourceIdAttributeName;
	Attributes.SourceRevisionAttributeName = SourceRevisionAttributeName;
	Attributes.SourceStateIdAttributeName = SourceStateIdAttributeName;

	Attributes.bOutputParentIndex = bOutputParentIndex;
	Attributes.ParentIndexAttributeName = ParentIndexAttributeName;
	Attributes.bOutputHierarchyLevel = bOutputHierarchyLevel;
	Attributes.HierarchyLevelAttributeName = HierarchyLevelAttributeName;
	Attributes.bOutputGeometryIndex = bOutputGeometryIndex;
	Attributes.GeometryIndexAttributeName = GeometryIndexAttributeName;

	Attributes.bOutputBoundsVolume = bOutputBoundsVolume;
	Attributes.BoundsVolumeAttributeName = BoundsVolumeAttributeName;

	Attributes.bOutputIsExterior = bOutputIsExterior;
	Attributes.IsExteriorAttributeName = IsExteriorAttributeName;
	Attributes.bOutputExposureRatio = bOutputExposureRatio;
	Attributes.ExposureRatioAttributeName = ExposureRatioAttributeName;
	Attributes.bOutputExteriorArea = bOutputExteriorArea;
	Attributes.ExteriorAreaAttributeName = ExteriorAreaAttributeName;
	Attributes.bOutputInteriorArea = bOutputInteriorArea;
	Attributes.InteriorAreaAttributeName = InteriorAreaAttributeName;
	Attributes.bOutputExteriorFaceCount = bOutputExteriorFaceCount;
	Attributes.ExteriorFaceCountAttributeName = ExteriorFaceCountAttributeName;
	Attributes.bOutputInteriorFaceCount = bOutputInteriorFaceCount;
	Attributes.InteriorFaceCountAttributeName = InteriorFaceCountAttributeName;

	return Attributes;
}

bool FPCGGeometryCollectionBonesToPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGGeometryCollectionBonesToPointsSettings* Settings = Context->GetInputSettings<UPCGGeometryCollectionBonesToPointsSettings>();
	check(Settings);

	static const FText NodeName = LOCTEXT("NodeName", "GC Bones To Points");

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, /*MeshData=*/nullptr, Settings->bOutputToWorldSpace);

	const FPCGUtilsGeometryCollectionBonePointAttributes Attributes = Settings->GetBonePointAttributes();

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* CollectionData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!CollectionData || !CollectionData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "GC Bones To Points skipped an input with no valid Geometry Collection."),
				Context);
			continue;
		}

		const FGeometryCollection& Collection = CollectionData->GetCollection();

		TArray<int32> Bones;
		PCGUtilsGeometryCollectionBonePoints::GatherBones(Collection, Settings->bIncludeClusterBones, Bones);
		if (Bones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoBones", "GC Bones To Points found no bones to emit."), Context);
			continue;
		}

		// The cluster pass needs the same global matrices the points were placed with, so the conversion
		// hands them back rather than making this node recompute them.
		TArray<FTransform> GlobalTransforms;
		UPCGPointArrayData* OutputData = PCGUtilsGeometryCollectionBonePoints::Build(
			Context, *CollectionData, Bones, LocalToWorld, Attributes, NodeName, &GlobalTransforms);
		if (!OutputData)
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionBonesToPointsConstants::PointsOutputPin;

		int32 NumClusterEdges = 0;
		if (Settings->bOutputCluster)
		{
			NumClusterEdges = EmitCluster(
				Context, Settings, Collection, Bones, LocalToWorld, GlobalTransforms, OutputData, Output);
		}

		UE_LOG(LogPCGUtilsFracture, Verbose,
			TEXT("GC Bones To Points: emitted %d point(s) for revision %d"),
			Bones.Num(), CollectionData->GetRevision());

		if (Settings->bOutputCluster)
		{
			UE_LOG(LogPCGUtilsFracture, Verbose,
				TEXT("GC Bones To Points: cluster with %d vtx and %d edge(s)"), Bones.Num(), NumClusterEdges);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
