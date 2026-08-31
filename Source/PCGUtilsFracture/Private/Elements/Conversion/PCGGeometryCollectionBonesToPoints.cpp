// Copyright Max Harris

#include "Elements/Conversion/PCGGeometryCollectionBonesToPoints.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
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
	 * Resolves every attribute this node can write, honouring one toggle and one name per attribute.
	 *
	 * A null pointer means "not requested", so the write sites stay a flat list of guarded assignments rather
	 * than branching on settings a second time. An enabled attribute with an empty name is a graph error:
	 * silently dropping it would leave the user filtering on something that never appears.
	 */
	struct FAttributeWriters
	{
		FPCGMetadataAttribute<int32>* BoneIndex = nullptr;
		FPCGMetadataAttribute<int64>* SourceId = nullptr;
		FPCGMetadataAttribute<int32>* SourceRevision = nullptr;
		FPCGMetadataAttribute<int64>* SourceStateId = nullptr;

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
			FPCGMetadataDomain* Domain, const UPCGGeometryCollectionBonesToPointsSettings* Settings, FPCGContext* Context)
		{
			bool bOk = true;

			auto Make = [Domain, Context, &bOk]<typename ValueType>(
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
						LOCTEXT("UnnamedAttribute",
							"GC Bones To Points has {0} enabled but its attribute name is empty."),
						FText::FromString(Label)), Context);
					bOk = false;
					return;
				}
				OutAttribute = Domain->FindOrCreateAttribute<ValueType>(Name, DefaultValue, false, true);
			};

			// Identity is unconditional - it is the contract with Select Bones From Points - but the names are
			// still user-supplied, so they get the same empty-name check.
			Make(BoneIndex, true, Settings->BoneIndexAttributeName, int32(INDEX_NONE), TEXT("Bone Index"));
			Make(SourceId, true, Settings->SourceIdAttributeName, int64(0), TEXT("Source Id"));
			Make(SourceRevision, true, Settings->SourceRevisionAttributeName, int32(INDEX_NONE),
				TEXT("Source Revision"));
			Make(SourceStateId, true, Settings->SourceStateIdAttributeName, int64(0), TEXT("Source State Id"));

			Make(ParentIndex, Settings->bOutputParentIndex, Settings->ParentIndexAttributeName,
				int32(INDEX_NONE), TEXT("Parent Index"));
			Make(HierarchyLevel, Settings->bOutputHierarchyLevel, Settings->HierarchyLevelAttributeName,
				int32(INDEX_NONE), TEXT("Hierarchy Level"));
			Make(GeometryIndex, Settings->bOutputGeometryIndex, Settings->GeometryIndexAttributeName,
				int32(INDEX_NONE), TEXT("Geometry Index"));
			Make(BoundsVolume, Settings->bOutputBoundsVolume, Settings->BoundsVolumeAttributeName,
				double(0.0), TEXT("Bounds Volume"));

			Make(IsExterior, Settings->bOutputIsExterior, Settings->IsExteriorAttributeName,
				false, TEXT("Is Exterior"));
			Make(ExposureRatio, Settings->bOutputExposureRatio, Settings->ExposureRatioAttributeName,
				double(0.0), TEXT("Exposure Ratio"));
			Make(ExteriorArea, Settings->bOutputExteriorArea, Settings->ExteriorAreaAttributeName,
				double(0.0), TEXT("Exterior Area"));
			Make(InteriorArea, Settings->bOutputInteriorArea, Settings->InteriorAreaAttributeName,
				double(0.0), TEXT("Interior Area"));
			Make(ExteriorFaceCount, Settings->bOutputExteriorFaceCount,
				Settings->ExteriorFaceCountAttributeName, int32(0), TEXT("Exterior Face Count"));
			Make(InteriorFaceCount, Settings->bOutputInteriorFaceCount,
				Settings->InteriorFaceCountAttributeName, int32(0), TEXT("Interior Face Count"));

			return bOk;
		}
	};
}

#if WITH_EDITOR
FText UPCGGeometryCollectionBonesToPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC Bones To Points");
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
	return {FPCGPinProperties(PCGGeometryCollectionBonesToPointsConstants::PointsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGGeometryCollectionBonesToPointsSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionBonesToPointsElement>();
}

bool FPCGGeometryCollectionBonesToPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGGeometryCollectionBonesToPointsSettings* Settings = Context->GetInputSettings<UPCGGeometryCollectionBonesToPointsSettings>();
	check(Settings);

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, /*MeshData=*/nullptr, Settings->bOutputToWorldSpace);

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
			PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Collection, Bones);
		}

		if (Bones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoBones", "GC Bones To Points found no bones to emit."), Context);
			continue;
		}

		// Stored bone transforms are parent-relative; anything spatial has to go through the global matrices.
		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);

		// The collection's Level attribute is often absent, and the facade's GenerateLevelAttribute() would
		// have to mutate the (immutable) input collection to add it. Walking Parent is cheap and read-only.
		TArray<int32> HierarchyLevels;
		if (Settings->bOutputHierarchyLevel)
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
			const FBox LocalBounds = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, BoneIndex);

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

		// One toggle and one name per attribute, so the details panel is the list of what this node can
		// produce and nothing is written that was not asked for.
		FAttributeWriters Writers;
		if (!Writers.Create(ElementsDomain, Settings, Context))
		{
			continue;
		}

		const bool bNeedsSurface = Settings->NeedsSurfaceInfo();
		const int64 SourceId = PCGUtilsGeometryCollectionIdentity::FoldGuid(CollectionData->GetCollectionId());
		const int64 SourceStateId = PCGUtilsGeometryCollectionIdentity::FoldGuid(CollectionData->GetStateId());
		const int32 SourceRevision = CollectionData->GetRevision();

		int32 NumExteriorBones = 0;
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

			Writers.BoneIndex->SetValue(Entry, BoneIndex);
			Writers.SourceId->SetValue(Entry, SourceId);
			Writers.SourceRevision->SetValue(Entry, SourceRevision);
			Writers.SourceStateId->SetValue(Entry, SourceStateId);

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

				NumExteriorBones += Surface.IsExterior() ? 1 : 0;
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionBonesToPointsConstants::PointsOutputPin;

		UE_LOG(LogPCGUtilsFracture, Verbose,
			TEXT("GC Bones To Points: emitted %d point(s) for revision %d%s"),
			Bones.Num(), SourceRevision,
			bNeedsSurface
				? *FString::Printf(TEXT(" (%d exterior, %d buried)"),
					NumExteriorBones, Bones.Num() - NumExteriorBones)
				: TEXT(""));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
