// Copyright Max Harris

#include "Elements/Edit/PCGGeometryCollectionTransformBones.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCTransformBones"

namespace
{
	/** Everything the points said about one bone, before duplicates have been resolved. */
	struct FBoneTargetAccumulator
	{
		/** Output-space deltas, in the order the points arrived. */
		TArray<FTransform> Deltas;
	};

	/** Counts worth reporting once at the end rather than per point. */
	struct FGatherStats
	{
		int32 NumConsidered = 0;
		int32 NumStale = 0;
		int32 NumNegative = 0;
		int32 NumOutOfRange = 0;
		int32 NumUnselected = 0;
		int32 NumNonFinite = 0;
		int32 StaleRevision = INDEX_NONE;
		bool bAnyStale = false;
	};

	/**
	 * Mean of several rotations, as a normalised linear blend with hemisphere correction.
	 *
	 * Not a true Riemannian mean, which matters not at all here: these are duplicate points naming one bone, so
	 * the inputs are clustered and NLERP and SLERP agree to well under a degree. Worth being explicit about,
	 * because the approximation would be wrong for widely separated rotations.
	 */
	FQuat AverageRotations(TConstArrayView<FTransform> InTransforms)
	{
		check(!InTransforms.IsEmpty());
		const FQuat Reference = InTransforms[0].GetRotation();
		FVector4 Sum(Reference.X, Reference.Y, Reference.Z, Reference.W);

		for (int32 Index = 1; Index < InTransforms.Num(); ++Index)
		{
			FQuat Rotation = InTransforms[Index].GetRotation();
			// q and -q are the same rotation; without this a pair either side of the hemisphere boundary
			// cancels out instead of averaging.
			if ((Rotation | Reference) < 0.0)
			{
				Rotation = FQuat(-Rotation.X, -Rotation.Y, -Rotation.Z, -Rotation.W);
			}
			Sum += FVector4(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
		}

		const FQuat Blended(Sum.X, Sum.Y, Sum.Z, Sum.W);
		// Exactly opposed rotations sum to zero and have no meaningful mean; the reference is as good an answer
		// as any and keeps the result finite.
		return Blended.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? Blended.GetNormalized() : Reference;
	}

	/** Component-wise mean of a set of deltas. */
	FTransform AverageTransforms(TConstArrayView<FTransform> InTransforms)
	{
		FVector SumTranslation = FVector::ZeroVector;
		FVector SumScale = FVector::ZeroVector;
		for (const FTransform& Transform : InTransforms)
		{
			SumTranslation += Transform.GetTranslation();
			SumScale += Transform.GetScale3D();
		}

		const double Count = static_cast<double>(InTransforms.Num());
		return FTransform(
			AverageRotations(InTransforms), SumTranslation / Count, SumScale / Count);
	}

	/**
	 * The delta one point asks for, with the components the user disabled taken from the reference instead.
	 *
	 * The filter has to be applied to the *point*, before the delta is derived - not to the bone frame the
	 * delta solves for. A point sits at the piece's centre, so rotating it necessarily swings the bone's origin
	 * around that centre; filtering afterwards would leave that displacement in the "translation" and make
	 * Translation Only move a piece by something other than the distance its point moved.
	 */
	FTransform ComputeFilteredDelta(
		const UPCGGeometryCollectionTransformBonesSettings* Settings,
		const FTransform& Reference,
		const FTransform& PointTransform)
	{
		const FTransform Filtered(
			Settings->bApplyRotation ? PointTransform.GetRotation() : Reference.GetRotation(),
			Settings->bApplyTranslation ? PointTransform.GetTranslation() : Reference.GetTranslation(),
			Settings->bApplyScale ? PointTransform.GetScale3D() : Reference.GetScale3D());

		// Reference * Delta == Filtered. A point nothing touched gives identity, which is what makes the round
		// trip exact.
		return Reference.Inverse() * Filtered;
	}

	/**
	 * Reads every point on the Points pin and records, per bone, how its transform differs from the transform
	 * that bone currently produces.
	 *
	 * Deltas rather than absolutes: a point sits at the piece's bounds centre, not the bone's origin, so the
	 * incoming transform is not the bone's transform and never was. Measuring the difference against a freshly
	 * recomputed reference is also what makes the operation a no-op when nothing upstream touched the points.
	 *
	 * @return false if the points cannot be used at all (already logged).
	 */
	bool GatherBoneTargets(
		FPCGContext* Context,
		const UPCGGeometryCollectionTransformBonesSettings* Settings,
		const UPCGGeometryCollectionData& CollectionData,
		const FGeometryCollection& Collection,
		TConstArrayView<FTransform> GlobalTransforms,
		const FTransform& LocalToWorld,
		const FDataflowTransformSelection* Selection,
		TMap<int32, FBoneTargetAccumulator>& OutTargets,
		FGatherStats& OutStats)
	{
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

		const int64 TargetSourceId =
			PCGUtilsGeometryCollectionIdentity::FoldGuid(CollectionData.GetCollectionId());
		const int64 TargetStateId =
			PCGUtilsGeometryCollectionIdentity::FoldGuid(CollectionData.GetStateId());

		// The reference transform is the same one GC | Bones To Points writes, so it is only computed for bones
		// that a point actually named.
		TMap<int32, FTransform> ReferenceCache;

		bool bAnyPointData = false;
		for (const FPCGTaggedData& Input :
			Context->InputData.GetInputsByPin(PCGGeometryCollectionTransformBonesConstants::PointsInputPin))
		{
			const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data);
			if (!Points)
			{
				continue;
			}
			bAnyPointData = true;

			const UPCGMetadata* PointMetadata = Points->ConstMetadata();
			const FPCGMetadataDomain* ElementsDomain = PointMetadata
				? PointMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
			const FPCGMetadataAttribute<int32>* BoneAttribute = ElementsDomain
				? ElementsDomain->GetConstTypedAttribute<int32>(Settings->BoneIndexAttributeName) : nullptr;
			if (!BoneAttribute)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("MissingAttribute",
						"GC Transform Bones skipped point data without the integer attribute '{0}'."),
					FText::FromName(Settings->BoneIndexAttributeName)), Context);
				continue;
			}

			const FPCGMetadataAttribute<int64>* SourceIdAttr = Settings->bValidateSourceIdentity
				? ElementsDomain->GetConstTypedAttribute<int64>(
					PCGUtilsGeometryCollectionIdentity::SourceIdAttribute) : nullptr;
			const FPCGMetadataAttribute<int64>* StateIdAttr = Settings->bValidateSourceIdentity
				? ElementsDomain->GetConstTypedAttribute<int64>(
					PCGUtilsGeometryCollectionIdentity::SourceStateIdAttribute) : nullptr;
			const FPCGMetadataAttribute<int32>* RevisionAttr = Settings->bValidateSourceIdentity
				? ElementsDomain->GetConstTypedAttribute<int32>(
					PCGUtilsGeometryCollectionIdentity::SourceRevisionAttribute) : nullptr;

			if (Settings->bValidateSourceIdentity && (!SourceIdAttr || !StateIdAttr))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingProvenance",
						"GC Transform Bones found '{0}' but no GC source identity on the same points. Use GC "
						"Bones To Points to author them, or disable Validate Source Identity."),
					FText::FromName(Settings->BoneIndexAttributeName)), Context);
				return false;
			}

			const auto Entries = Points->GetConstMetadataEntryValueRange();
			const auto Transforms = Points->GetConstTransformValueRange();
			const int32 NumPoints = FMath::Min(Entries.Num(), Transforms.Num());

			for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
			{
				++OutStats.NumConsidered;
				const PCGMetadataEntryKey Entry = Entries[PointIndex];

				if (Settings->bValidateSourceIdentity)
				{
					// StateId is the authoritative check: unique per collection state, so it catches both
					// "different collection" and "same collection, different state" in one comparison.
					if (StateIdAttr->GetValueFromItemKey(Entry) != TargetStateId
						|| SourceIdAttr->GetValueFromItemKey(Entry) != TargetSourceId)
					{
						++OutStats.NumStale;
						if (!OutStats.bAnyStale)
						{
							OutStats.bAnyStale = true;
							OutStats.StaleRevision =
								RevisionAttr ? RevisionAttr->GetValueFromItemKey(Entry) : INDEX_NONE;
						}
						continue;
					}
				}

				const int32 BoneIndex = BoneAttribute->GetValueFromItemKey(Entry);
				if (BoneIndex < 0)
				{
					++OutStats.NumNegative;
					continue;
				}
				if (BoneIndex >= NumTransforms)
				{
					++OutStats.NumOutOfRange;
					continue;
				}
				if (Selection && (!Selection->IsValidIndex(BoneIndex) || !Selection->IsSelected(BoneIndex)))
				{
					++OutStats.NumUnselected;
					continue;
				}

				const FTransform& PointTransform = Transforms[PointIndex];
				if (PointTransform.ContainsNaN())
				{
					++OutStats.NumNonFinite;
					continue;
				}

				FTransform Reference;
				if (const FTransform* Cached = ReferenceCache.Find(BoneIndex))
				{
					Reference = *Cached;
				}
				else
				{
					Reference = (Settings->PointPivot == EPCGGeometryCollectionBonePointPivot::PieceCentre)
						? PCGUtilsGeometryCollectionTransforms::ComputeBonePointTransform(
							Collection, BoneIndex, GlobalTransforms, LocalToWorld)
						: PCGUtilsGeometryCollectionTransforms::ComputeBoneOriginTransform(
							BoneIndex, GlobalTransforms, LocalToWorld);
					ReferenceCache.Add(BoneIndex, Reference);
				}

				OutTargets.FindOrAdd(BoneIndex).Deltas.Add(
					ComputeFilteredDelta(Settings, Reference, PointTransform));
			}
		}

		if (!bAnyPointData)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoPoints", "GC Transform Bones received no point data on its Points pin."), Context);
		}

		if (OutStats.bAnyStale)
		{
			// A hard error, not a warning: silently moving the wrong fracture pieces is far worse than failing.
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("StaleSource",
					"GC Transform Bones rejected {0} of {1} points authored against a different collection state "
					"(points say revision {2}, target is revision {3}). Re-run GC Bones To Points against the "
					"collection you are transforming."),
				FText::AsNumber(OutStats.NumStale), FText::AsNumber(OutStats.NumConsidered),
				FText::AsNumber(OutStats.StaleRevision), FText::AsNumber(CollectionData.GetRevision())), Context);
			return false;
		}

		return true;
	}

	/** Collapses each bone's accumulated deltas to one, per the duplicate policy. Logs and fails on Error. */
	bool ResolveDuplicates(
		FPCGContext* Context,
		const UPCGGeometryCollectionTransformBonesSettings* Settings,
		const TMap<int32, FBoneTargetAccumulator>& InTargets,
		TArray<int32>& OutBones,
		TArray<FTransform>& OutDeltas)
	{
		TArray<int32> DuplicatedBones;
		for (const TPair<int32, FBoneTargetAccumulator>& Pair : InTargets)
		{
			if (Pair.Value.Deltas.Num() > 1)
			{
				DuplicatedBones.Add(Pair.Key);
			}
		}

		if (!DuplicatedBones.IsEmpty()
			&& Settings->DuplicateBoneHandling == EPCGGeometryCollectionDuplicateBoneHandling::Error)
		{
			DuplicatedBones.Sort();
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("DuplicateBones",
					"GC Transform Bones found {0} bone(s) named by more than one point (first is bone {1}). Each "
					"bone can only have one transform - filter the points, or choose a Duplicate Bone Handling "
					"mode."),
				FText::AsNumber(DuplicatedBones.Num()), FText::AsNumber(DuplicatedBones[0])), Context);
			return false;
		}

		InTargets.GetKeys(OutBones);
		// Sorted so the operation is reproducible regardless of map iteration order.
		OutBones.Sort();

		OutDeltas.Reserve(OutBones.Num());
		for (const int32 Bone : OutBones)
		{
			const TArray<FTransform>& Deltas = InTargets[Bone].Deltas;
			switch (Settings->DuplicateBoneHandling)
			{
			case EPCGGeometryCollectionDuplicateBoneHandling::Last:
				OutDeltas.Add(Deltas.Last());
				break;
			case EPCGGeometryCollectionDuplicateBoneHandling::Average:
				OutDeltas.Add(Deltas.Num() > 1 ? AverageTransforms(Deltas) : Deltas[0]);
				break;
			case EPCGGeometryCollectionDuplicateBoneHandling::Error:
			case EPCGGeometryCollectionDuplicateBoneHandling::First:
			default:
				OutDeltas.Add(Deltas[0]);
				break;
			}
		}

		if (!DuplicatedBones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("DuplicateBonesResolved",
					"GC Transform Bones resolved {0} bone(s) named by more than one point."),
				FText::AsNumber(DuplicatedBones.Num())), Context);
		}

		return true;
	}

	/** The collection-space transform a bone should end up with, given the delta its point asked for. */
	FTransform ResolveDesiredGlobal(
		const int32 BoneIndex,
		TConstArrayView<FTransform> GlobalTransforms,
		const FTransform& LocalToWorld,
		const FTransform& DeltaInOutputSpace)
	{
		const FTransform CurrentFrame = PCGUtilsGeometryCollectionTransforms::ComputeBoneOriginTransform(
			BoneIndex, GlobalTransforms, LocalToWorld);

		// Strip the output-space placement back off: GetRelativeTransform(X) is `this * X^-1`.
		return (CurrentFrame * DeltaInOutputSpace).GetRelativeTransform(LocalToWorld);
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionTransformBonesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Transform Bones");
}

FText UPCGGeometryCollectionTransformBonesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Writes point transforms back onto the Geometry Collection bones they came from, completing the round "
		"trip GC Bones To Points starts. Move, rotate or randomise the bone points with any PCG node in "
		"between. Only the difference from where each point started is applied, so untouched points change "
		"nothing. Transforming a cluster moves everything beneath it and preserves their relative placement.");
}

FString UPCGGeometryCollectionTransformBonesSettings::GetAdditionalTitleInformation() const
{
	// Translation + rotation is the default and says nothing worth the graph width. Anything else changes what
	// the node visibly does, which is exactly what a subtitle is for.
	if (bApplyTranslation && bApplyRotation && !bApplyScale)
	{
		return FString();
	}
	if (AppliesNothing())
	{
		return TEXT("Nothing");
	}

	TArray<FString> Parts;
	if (bApplyTranslation) { Parts.Add(TEXT("Move")); }
	if (bApplyRotation) { Parts.Add(TEXT("Rotate")); }
	if (bApplyScale) { Parts.Add(TEXT("Scale")); }
	return FString::Join(Parts, TEXT(" + "));
}
#endif

TArray<FPCGPinProperties> UPCGGeometryCollectionTransformBonesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGeometryCollectionTransformBonesConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	Pins.Emplace_GetRef(
		PCGGeometryCollectionTransformBonesConstants::PointsInputPin,
		EPCGDataType::Point, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	// Optional: the Points pin is already a selection mechanism, since a bone with no point is not touched. A
	// Selection narrows that further without having to filter the points themselves.
	Pins.Emplace(
		PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionTransformBonesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGeometryCollectionTransformBonesConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), true, true));
	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionTransformBonesSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionTransformBonesElement>();
}

bool FPCGGeometryCollectionTransformBonesElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGGeometryCollectionTransformBonesSettings* Settings =
		Context->GetInputSettings<UPCGGeometryCollectionTransformBonesSettings>();
	check(Settings);

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, /*MeshData=*/nullptr, Settings->bPointsAreWorldSpace);

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGeometryCollectionTransformBonesConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* InputData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!InputData || !InputData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "GC Transform Bones skipped an input with no valid Geometry Collection."),
				Context);
			continue;
		}

		if (InputData->NumTransforms() == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "GC Transform Bones received an empty Geometry Collection."), Context);
			continue;
		}

		// Passing the input straight through preserves its object identity and StateId, so any selection
		// authored against it downstream still resolves. Publishing an identical revision would break that for
		// no benefit.
		auto PassThrough = [Context, &Input]()
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Output.Pin = PCGGeometryCollectionTransformBonesConstants::CollectionOutputPin;
		};

		if (Settings->AppliesNothing())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NothingApplied",
					"GC Transform Bones has translation, rotation and scale all disabled, so it changed nothing."),
				Context);
			PassThrough();
			continue;
		}

		TSharedRef<FGeometryCollection> Collection = InputData->CreateMutableCopy();

		FDataflowTransformSelection Selection;
		bool bHasSelection = false;
		{
			const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(*InputData, *Collection);
			if (!PCGUtilsGeometryCollectionSelectionFactories::ResolveSelectionFromPin(
				Context, PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin,
				EvaluationContext, /*bRequired=*/false, Selection, bHasSelection))
			{
				continue;
			}
		}

		// Resolved once, before anything is written: every delta is measured against the collection as it
		// arrived, so the result cannot depend on the order bones happen to be processed in.
		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(*Collection, GlobalTransforms);

		TMap<int32, FBoneTargetAccumulator> Targets;
		FGatherStats Stats;
		if (!GatherBoneTargets(
			Context, Settings, *InputData, *Collection, GlobalTransforms, LocalToWorld,
			bHasSelection ? &Selection : nullptr, Targets, Stats))
		{
			continue;
		}

		if (Stats.NumNegative > 0 || Stats.NumOutOfRange > 0 || Stats.NumNonFinite > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InvalidIndices",
					"GC Transform Bones ignored {0} negative, {1} out-of-range bone index/indices and {2} point(s) "
					"with a non-finite transform (collection has {3} bones)."),
				FText::AsNumber(Stats.NumNegative), FText::AsNumber(Stats.NumOutOfRange),
				FText::AsNumber(Stats.NumNonFinite), FText::AsNumber(InputData->NumTransforms())), Context);
		}

		if (Targets.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoTargets", "GC Transform Bones resolved no bones to transform."), Context);
			PassThrough();
			continue;
		}

		TArray<int32> Bones;
		TArray<FTransform> Deltas;
		if (!ResolveDuplicates(Context, Settings, Targets, Bones, Deltas))
		{
			continue;
		}

		TArray<FTransform> DesiredGlobals;
		DesiredGlobals.Reserve(Bones.Num());
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			DesiredGlobals.Add(
				ResolveDesiredGlobal(Bones[Index], GlobalTransforms, LocalToWorld, Deltas[Index]));
		}

		FPCGUtilsGeometryCollectionBoneTransformResult ApplyResult;
		if (!PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
			*Collection, Bones, DesiredGlobals, Settings->NestedBoneHandling, ApplyResult))
		{
			// The only refusal the library reports for a well-formed request.
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NestedTargets",
					"GC Transform Bones was asked to transform a bone and one of its ancestors at the same time, "
					"with Nested Bone Handling set to Error. Moving a cluster already moves everything beneath "
					"it; choose Topmost or Independent to say which you meant."), Context);
			continue;
		}

		if (!ApplyResult.NestedBonesDropped.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NestedDropped",
					"GC Transform Bones skipped {0} bone(s) whose ancestor was also transformed; the ancestor's "
					"move carries them."),
				FText::AsNumber(ApplyResult.NestedBonesDropped.Num())), Context);
		}

		if (!ApplyResult.UnrepresentableBones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("Unrepresentable",
					"GC Transform Bones could not represent the requested transform for {0} bone(s), usually a "
					"zero or sheared scale, and left them unchanged."),
				FText::AsNumber(ApplyResult.UnrepresentableBones.Num())), Context);
		}

		if (ApplyResult.NumApplied == 0)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NothingMoved", "GC Transform Bones transformed no bones."), Context);
			PassThrough();
			continue;
		}

		// Transforms only: no bone was added, removed or reparented, and geometry is stored bone-local so not a
		// vertex changed. The publisher carries the piece mesh cache across unchanged and drops Proximity, which
		// is the one derived thing moving a bone invalidates.
		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bTransformsChanged = true;

		UPCGGeometryCollectionData* OutputData = PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			Context, InputData, Collection, Mutation);
		if (!OutputData)
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionTransformBonesConstants::CollectionOutputPin;

		UE_LOG(LogPCGUtilsFracture, Log,
			TEXT("GC Transform Bones: %d point(s) -> %d bone(s) transformed (%d skipped as nested, %d not "
				"selected). Result %s (revision %d)"),
			Stats.NumConsidered, ApplyResult.NumApplied, ApplyResult.NestedBonesDropped.Num(),
			Stats.NumUnselected,
			*PCGUtilsGeometryCollectionHelpers::DescribeCollection(*Collection), OutputData->GetRevision());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
