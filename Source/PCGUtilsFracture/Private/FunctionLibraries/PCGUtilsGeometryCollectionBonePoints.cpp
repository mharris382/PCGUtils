// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionBonePoints.h"

#include "Data/PCGPointArrayData.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGCBonePoints"

namespace
{
	/**
	 * Resolves every attribute a bone-point conversion can write, honouring one toggle and one name each.
	 *
	 * A null pointer means "not requested", so the write sites stay a flat list of guarded assignments rather
	 * than branching on the options a second time. An enabled attribute with an empty name is a graph error:
	 * silently dropping it would leave the user filtering on something that never appears.
	 */
	struct FBonePointAttributeWriters
	{
		FPCGMetadataAttribute<int32>* BoneIndex = nullptr;
		FPCGMetadataAttribute<int64>* SourceId = nullptr;
		FPCGMetadataAttribute<int32>* SourceRevision = nullptr;
		FPCGMetadataAttribute<int64>* SourceStateId = nullptr;
		FPCGMetadataAttribute<int64>* BoneId = nullptr;

		FPCGMetadataAttribute<int32>* ParentIndex = nullptr;
		FPCGMetadataAttribute<int32>* HierarchyLevel = nullptr;
		FPCGMetadataAttribute<int32>* GeometryIndex = nullptr;
		FPCGMetadataAttribute<double>* BoundsVolume = nullptr;

		FPCGMetadataAttribute<bool>* IsExterior = nullptr;
		FPCGMetadataAttribute<double>* ExposureRatio = nullptr;
		FPCGMetadataAttribute<double>* ExteriorArea = nullptr;
		FPCGMetadataAttribute<double>* InteriorArea = nullptr;
		FPCGMetadataAttribute<int32>* ExteriorFaceCount = nullptr;
		FPCGMetadataAttribute<int32>* InteriorFaceCount = nullptr;

		bool Create(
			FPCGMetadataDomain* Domain,
			const FPCGUtilsGeometryCollectionBonePointAttributes& Options,
			const FText& NodeName,
			FPCGContext* Context)
		{
			bool bOk = true;

			auto Make = [Domain, &NodeName, Context, &bOk]<typename ValueType>(
				FPCGMetadataAttribute<ValueType>*& OutAttribute, bool bEnabled, FName Name,
				ValueType DefaultValue, const TCHAR* Label)
			{
				OutAttribute = nullptr;
				if (!bEnabled)
				{
					return;
				}
				if (Name.IsNone())
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("UnnamedAttribute", "{0} has {1} enabled but its attribute name is empty."),
						NodeName, FText::FromString(Label)), Context);
					bOk = false;
					return;
				}
				OutAttribute = Domain->FindOrCreateAttribute<ValueType>(Name, DefaultValue, false, true);
			};

			// Identity is unconditional - it is the contract with Select Bones From Points - but the names are
			// still user-supplied, so they get the same empty-name check.
			Make(BoneIndex, true, Options.BoneIndexAttributeName, int32(INDEX_NONE), TEXT("Bone Index"));
			Make(SourceId, true, Options.SourceIdAttributeName, int64(0), TEXT("Source Id"));
			Make(SourceRevision, true, Options.SourceRevisionAttributeName, int32(INDEX_NONE),
				TEXT("Source Revision"));
			Make(SourceStateId, true, Options.SourceStateIdAttributeName, int64(0), TEXT("Source State Id"));
			Make(BoneId, Options.bOutputBoneId, Options.BoneIdAttributeName, int64(0), TEXT("Bone Id"));

			Make(ParentIndex, Options.bOutputParentIndex, Options.ParentIndexAttributeName,
				int32(INDEX_NONE), TEXT("Parent Index"));
			Make(HierarchyLevel, Options.bOutputHierarchyLevel, Options.HierarchyLevelAttributeName,
				int32(INDEX_NONE), TEXT("Hierarchy Level"));
			Make(GeometryIndex, Options.bOutputGeometryIndex, Options.GeometryIndexAttributeName,
				int32(INDEX_NONE), TEXT("Geometry Index"));
			Make(BoundsVolume, Options.bOutputBoundsVolume, Options.BoundsVolumeAttributeName,
				double(0.0), TEXT("Bounds Volume"));

			Make(IsExterior, Options.bOutputIsExterior, Options.IsExteriorAttributeName,
				false, TEXT("Is Exterior"));
			Make(ExposureRatio, Options.bOutputExposureRatio, Options.ExposureRatioAttributeName,
				double(0.0), TEXT("Exposure Ratio"));
			Make(ExteriorArea, Options.bOutputExteriorArea, Options.ExteriorAreaAttributeName,
				double(0.0), TEXT("Exterior Area"));
			Make(InteriorArea, Options.bOutputInteriorArea, Options.InteriorAreaAttributeName,
				double(0.0), TEXT("Interior Area"));
			Make(ExteriorFaceCount, Options.bOutputExteriorFaceCount,
				Options.ExteriorFaceCountAttributeName, int32(0), TEXT("Exterior Face Count"));
			Make(InteriorFaceCount, Options.bOutputInteriorFaceCount,
				Options.InteriorFaceCountAttributeName, int32(0), TEXT("Interior Face Count"));

			return bOk;
		}
	};
}

void PCGUtilsGeometryCollectionBonePoints::GatherBones(
	const FGeometryCollection& InCollection, const bool bIncludeClusterBones, TArray<int32>& OutBones)
{
	OutBones.Reset();

	if (!bIncludeClusterBones)
	{
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(InCollection, OutBones);
		return;
	}

	const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
	OutBones.Reserve(NumTransforms);
	for (int32 BoneIndex = 0; BoneIndex < NumTransforms; ++BoneIndex)
	{
		OutBones.Add(BoneIndex);
	}
}

UPCGPointArrayData* PCGUtilsGeometryCollectionBonePoints::Build(
	FPCGContext* InContext,
	const UPCGGeometryCollectionData& InCollectionData,
	const TConstArrayView<int32> InBones,
	const FTransform& InLocalToWorld,
	const FPCGUtilsGeometryCollectionBonePointAttributes& InAttributes,
	const FText& InNodeNameForMessages,
	TArray<FTransform>* OutGlobalTransforms)
{
	const FGeometryCollection& Collection = InCollectionData.GetCollection();
	const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

	// Stored bone transforms are parent-relative; anything spatial has to go through the global matrices.
	TArray<FTransform> LocalGlobalTransforms;
	TArray<FTransform>& GlobalTransforms = OutGlobalTransforms ? *OutGlobalTransforms : LocalGlobalTransforms;
	PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);

	// Every collection this module publishes carries a Level attribute, and the hierarchy service reads it
	// - falling back to a Parent walk only for a collection that somehow arrived without one, so the
	// fallback lives in one place instead of here.
	TArray<int32> HierarchyLevels;
	if (InAttributes.bOutputHierarchyLevel)
	{
		HierarchyLevels.Reserve(NumTransforms);
		for (int32 BoneIndex = 0; BoneIndex < NumTransforms; ++BoneIndex)
		{
			HierarchyLevels.Add(PCGUtilsGeometryCollectionHierarchy::GetLevel(Collection, BoneIndex));
		}
	}

	UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(InContext);
	OutputData->SetNumPoints(InBones.Num(), /*bInitializeValues=*/false);
	auto Transforms = OutputData->GetTransformValueRange();
	auto Densities = OutputData->GetDensityValueRange();
	auto BoundsMin = OutputData->GetBoundsMinValueRange();
	auto BoundsMax = OutputData->GetBoundsMaxValueRange();
	auto Colors = OutputData->GetColorValueRange();

	for (int32 Index = 0; Index < InBones.Num(); ++Index)
	{
		const int32 BoneIndex = InBones[Index];
		const FBox LocalBounds = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, BoneIndex);
		const FVector LocalCenter = LocalBounds.IsValid ? LocalBounds.GetCenter() : FVector::ZeroVector;

		// The pivot convention - piece centre, bone orientation and scale - lives in the transforms library
		// rather than here, because GC | Transform Bones has to reconstruct exactly this transform to recover
		// what the user changed about a point. Two implementations of it would be a silent per-piece offset.
		Transforms[Index] = PCGUtilsGeometryCollectionTransforms::ComputeBonePointTransform(
			Collection, BoneIndex, GlobalTransforms, InLocalToWorld);

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
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("NoMetadata", "{0} could not create point metadata."), InNodeNameForMessages), InContext);
		return nullptr;
	}

	FBonePointAttributeWriters Writers;
	if (!Writers.Create(ElementsDomain, InAttributes, InNodeNameForMessages, InContext))
	{
		return nullptr;
	}

	const bool bNeedsSurface = InAttributes.NeedsSurfaceInfo();
	const int64 SourceId = PCGUtilsGeometryCollectionIdentity::FoldGuid(InCollectionData.GetCollectionId());
	const int64 SourceStateId = PCGUtilsGeometryCollectionIdentity::FoldGuid(InCollectionData.GetStateId());
	const int32 SourceRevision = InCollectionData.GetRevision();

	auto MetadataEntries = OutputData->GetMetadataEntryValueRange();
	for (int32 Index = 0; Index < InBones.Num(); ++Index)
	{
		const int32 BoneIndex = InBones[Index];
		// SetNumPoints(..., bInitializeValues=false) leaves these uninitialised, and InitializeOnSet only
		// allocates an entry for a key that is already invalid - so reset first or the garbage key
		// survives and every attribute written against it is unreadable downstream.
		MetadataEntries[Index] = PCGInvalidEntryKey;
		ElementsDomain->InitializeOnSet(MetadataEntries[Index]);
		const PCGMetadataEntryKey Entry = MetadataEntries[Index];

		Writers.BoneIndex->SetValue(Entry, BoneIndex);
		Writers.SourceId->SetValue(Entry, SourceId);
		Writers.SourceRevision->SetValue(Entry, SourceRevision);
		Writers.SourceStateId->SetValue(Entry, SourceStateId);

		if (Writers.BoneId)
		{
			// Folded to int64 the same way the source ids are, because PCG metadata has no FGuid type. A bone
			// with no id folds to 0, which is also the attribute default, so "no id" and "absent" agree.
			const FGuid BoneGuid = PCGUtilsGeometryCollectionIdentity::GetBoneId(Collection, BoneIndex);
			Writers.BoneId->SetValue(Entry, BoneGuid.IsValid()
				? PCGUtilsGeometryCollectionIdentity::FoldGuid(BoneGuid) : int64(0));
		}

		if (Writers.ParentIndex)
		{
			Writers.ParentIndex->SetValue(Entry, Collection.Parent.IsValidIndex(BoneIndex)
				? Collection.Parent[BoneIndex] : INDEX_NONE);
		}
		if (Writers.HierarchyLevel)
		{
			Writers.HierarchyLevel->SetValue(Entry, HierarchyLevels.IsValidIndex(BoneIndex)
				? HierarchyLevels[BoneIndex] : INDEX_NONE);
		}
		if (Writers.GeometryIndex)
		{
			Writers.GeometryIndex->SetValue(Entry,
				Collection.TransformToGeometryIndex.IsValidIndex(BoneIndex)
					? Collection.TransformToGeometryIndex[BoneIndex] : INDEX_NONE);
		}
		if (Writers.BoundsVolume)
		{
			const FBox LocalBounds = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, BoneIndex);
			Writers.BoundsVolume->SetValue(Entry, LocalBounds.IsValid ? LocalBounds.GetVolume() : 0.0);
		}

		if (bNeedsSurface)
		{
			// Areas are measured in collection space, so a scaled bone reports its real surface area.
			const FTransform& BoneToCollection = GlobalTransforms.IsValidIndex(BoneIndex)
				? GlobalTransforms[BoneIndex] : FTransform::Identity;
			const PCGUtilsGeometryCollectionHelpers::FBoneSurfaceInfo Surface =
				PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(Collection, BoneIndex, BoneToCollection);

			if (Writers.IsExterior) { Writers.IsExterior->SetValue(Entry, Surface.IsExterior()); }
			if (Writers.ExposureRatio) { Writers.ExposureRatio->SetValue(Entry, Surface.ExposureRatio()); }
			if (Writers.ExteriorArea) { Writers.ExteriorArea->SetValue(Entry, Surface.ExteriorArea); }
			if (Writers.InteriorArea) { Writers.InteriorArea->SetValue(Entry, Surface.InteriorArea); }
			if (Writers.ExteriorFaceCount)
			{
				Writers.ExteriorFaceCount->SetValue(Entry, Surface.ExteriorFaceCount);
			}
			if (Writers.InteriorFaceCount)
			{
				Writers.InteriorFaceCount->SetValue(Entry, Surface.InteriorFaceCount);
			}
		}
	}

	return OutputData;
}

#undef LOCTEXT_NAMESPACE
