// Copyright Max Harris

#include "Elements/Conversion/PCGGCBonesToPoints.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCBonesToPoints"

#if WITH_EDITOR
FText UPCGGCBonesToPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC Bones To Points");
}

FText UPCGGCBonesToPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Outputs one point per geometry-bearing bone, positioned at the piece's centre with the piece's bounds "
		"and orientation. Each point carries GC_BoneIndex plus the source collection's identity, so a "
		"downstream Select Bones From Points can prove the selection still matches the collection it was "
		"authored against. Filter these points with ordinary PCG/PCGEx nodes.");
}
#endif

TArray<FPCGPinProperties> UPCGGCBonesToPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGCBonesToPointsConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGGCBonesToPointsSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGGCBonesToPointsConstants::PointsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGGCBonesToPointsSettings::CreateElement() const
{
	return MakeShared<FPCGGCBonesToPointsElement>();
}

bool FPCGGCBonesToPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGGCBonesToPointsSettings* Settings = Context->GetInputSettings<UPCGGCBonesToPointsSettings>();
	check(Settings);

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, /*MeshData=*/nullptr, Settings->bOutputToWorldSpace);

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGCBonesToPointsConstants::CollectionInputPin))
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
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

		TArray<int32> Bones;
		if (Settings->bIncludeClusterBones)
		{
			Bones.Reserve(NumTransforms);
			for (int32 BoneIndex = 0; BoneIndex < NumTransforms; ++BoneIndex)
			{
				Bones.Add(BoneIndex);
			}
		}
		else
		{
			PCGUtilsGCHelpers::GatherGeometryBearingBones(Collection, Bones);
		}

		if (Bones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoBones", "GC Bones To Points found no bones to emit."), Context);
			continue;
		}

		// Stored bone transforms are parent-relative; anything spatial has to go through the global matrices.
		TArray<FTransform> GlobalTransforms;
		PCGUtilsGCHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);

		// The collection's Level attribute is often absent, and the facade's GenerateLevelAttribute() would
		// have to mutate the (immutable) input collection to add it. Walking Parent is cheap and read-only.
		TArray<int32> HierarchyLevels;
		if (Settings->bOutputExtraAttributes)
		{
			HierarchyLevels.Init(INDEX_NONE, NumTransforms);
			TFunction<int32(int32)> ResolveLevel = [&](int32 BoneIndex) -> int32
			{
				if (!HierarchyLevels.IsValidIndex(BoneIndex))
				{
					return INDEX_NONE;
				}
				if (HierarchyLevels[BoneIndex] != INDEX_NONE)
				{
					return HierarchyLevels[BoneIndex];
				}
				// Marked before recursing, so a malformed cycle terminates at 0 instead of overflowing.
				HierarchyLevels[BoneIndex] = 0;
				const int32 ParentIndex = Collection.Parent.IsValidIndex(BoneIndex)
					? Collection.Parent[BoneIndex] : INDEX_NONE;
				if (ParentIndex != INDEX_NONE)
				{
					HierarchyLevels[BoneIndex] = ResolveLevel(ParentIndex) + 1;
				}
				return HierarchyLevels[BoneIndex];
			};
			for (int32 BoneIndex = 0; BoneIndex < NumTransforms; ++BoneIndex)
			{
				ResolveLevel(BoneIndex);
			}
		}

		UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		OutputData->SetNumPoints(Bones.Num(), /*bInitializeValues=*/false);
		auto Transforms = OutputData->GetTransformValueRange();
		auto Densities = OutputData->GetDensityValueRange();
		auto BoundsMin = OutputData->GetBoundsMinValueRange();
		auto BoundsMax = OutputData->GetBoundsMaxValueRange();
		auto Colors = OutputData->GetColorValueRange();

		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const int32 BoneIndex = Bones[Index];
			const FTransform BoneToCollection = GlobalTransforms.IsValidIndex(BoneIndex)
				? GlobalTransforms[BoneIndex] : FTransform::Identity;
			const FBox LocalBounds = PCGUtilsGCHelpers::GetBoneLocalBounds(Collection, BoneIndex);

			const FVector LocalCenter = LocalBounds.IsValid ? LocalBounds.GetCenter() : FVector::ZeroVector;
			const FTransform BoneToOutput = BoneToCollection * LocalToWorld;

			// Keep the bone's orientation and scale: the point should represent the piece, not just mark
			// where it is. Only the translation moves to the piece's centre.
			Transforms[Index] = FTransform(
				BoneToOutput.GetRotation(),
				BoneToOutput.TransformPosition(LocalCenter),
				BoneToOutput.GetScale3D());

			// PCG point bounds are point-local, so they are the piece's own extents about that centre. This is
			// what makes ordinary PCG bounds-overlap filtering meaningful against a fracture piece.
			if (LocalBounds.IsValid)
			{
				BoundsMin[Index] = LocalBounds.Min - LocalCenter;
				BoundsMax[Index] = LocalBounds.Max - LocalCenter;
			}
			else
			{
				BoundsMin[Index] = FVector::ZeroVector;
				BoundsMax[Index] = FVector::ZeroVector;
			}

			Densities[Index] = 1.0f;
			const FLinearColor BoneColor = Collection.BoneColor.IsValidIndex(BoneIndex)
				? Collection.BoneColor[BoneIndex] : FLinearColor::White;
			Colors[Index] = FVector4(BoneColor);
		}

		UPCGMetadata* Metadata = OutputData->MutableMetadata();
		FPCGMetadataDomain* ElementsDomain =
			Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		if (!ElementsDomain)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoMetadata", "GC Bones To Points could not create point metadata."), Context);
			continue;
		}

		using namespace PCGUtilsGCIdentity;
		FPCGMetadataAttribute<int32>* BoneIndexAttr =
			ElementsDomain->FindOrCreateAttribute<int32>(BoneIndexAttribute, INDEX_NONE, false, true);
		FPCGMetadataAttribute<int64>* SourceIdAttr =
			ElementsDomain->FindOrCreateAttribute<int64>(SourceIdAttribute, 0, false, true);
		FPCGMetadataAttribute<int32>* SourceRevisionAttr =
			ElementsDomain->FindOrCreateAttribute<int32>(SourceRevisionAttribute, INDEX_NONE, false, true);
		FPCGMetadataAttribute<int64>* SourceStateIdAttr =
			ElementsDomain->FindOrCreateAttribute<int64>(SourceStateIdAttribute, 0, false, true);

		FPCGMetadataAttribute<int32>* ParentIndexAttr = nullptr;
		FPCGMetadataAttribute<int32>* HierarchyLevelAttr = nullptr;
		FPCGMetadataAttribute<int32>* GeometryIndexAttr = nullptr;
		FPCGMetadataAttribute<double>* BoundsVolumeAttr = nullptr;
		if (Settings->bOutputExtraAttributes)
		{
			ParentIndexAttr =
				ElementsDomain->FindOrCreateAttribute<int32>(ParentIndexAttribute, INDEX_NONE, false, true);
			HierarchyLevelAttr =
				ElementsDomain->FindOrCreateAttribute<int32>(HierarchyLevelAttribute, INDEX_NONE, false, true);
			GeometryIndexAttr =
				ElementsDomain->FindOrCreateAttribute<int32>(GeometryIndexAttribute, INDEX_NONE, false, true);
			BoundsVolumeAttr =
				ElementsDomain->FindOrCreateAttribute<double>(BoundsVolumeAttribute, 0.0, false, true);
		}

		const int64 SourceId = FoldGuid(CollectionData->GetCollectionId());
		const int64 SourceStateId = FoldGuid(CollectionData->GetStateId());
		const int32 SourceRevision = CollectionData->GetRevision();

		auto MetadataEntries = OutputData->GetMetadataEntryValueRange();
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const int32 BoneIndex = Bones[Index];
			// SetNumPoints(..., bInitializeValues=false) leaves these uninitialised, and InitializeOnSet only
			// allocates an entry for a key that is already invalid - so reset first or the garbage key
			// survives and every attribute written against it is unreadable downstream.
			MetadataEntries[Index] = PCGInvalidEntryKey;
			ElementsDomain->InitializeOnSet(MetadataEntries[Index]);
			const PCGMetadataEntryKey Entry = MetadataEntries[Index];

			BoneIndexAttr->SetValue(Entry, BoneIndex);
			SourceIdAttr->SetValue(Entry, SourceId);
			SourceRevisionAttr->SetValue(Entry, SourceRevision);
			SourceStateIdAttr->SetValue(Entry, SourceStateId);

			if (Settings->bOutputExtraAttributes)
			{
				ParentIndexAttr->SetValue(Entry, Collection.Parent.IsValidIndex(BoneIndex)
					? Collection.Parent[BoneIndex] : INDEX_NONE);
				HierarchyLevelAttr->SetValue(Entry, HierarchyLevels.IsValidIndex(BoneIndex)
					? HierarchyLevels[BoneIndex] : INDEX_NONE);
				GeometryIndexAttr->SetValue(Entry, Collection.TransformToGeometryIndex.IsValidIndex(BoneIndex)
					? Collection.TransformToGeometryIndex[BoneIndex] : INDEX_NONE);

				const FBox LocalBounds = PCGUtilsGCHelpers::GetBoneLocalBounds(Collection, BoneIndex);
				BoundsVolumeAttr->SetValue(Entry, LocalBounds.IsValid ? LocalBounds.GetVolume() : 0.0);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGCBonesToPointsConstants::PointsOutputPin;

		UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Bones To Points: emitted %d point(s) for revision %d"),
			Bones.Num(), SourceRevision);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
