// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/TransformCollection.h"

namespace
{
	/**
	 * A transform the collection can actually store and later compose with.
	 *
	 * Rejects non-finite values (a NaN would spread through every descendant's global matrix the next time
	 * anything asked for one) and zero scale on any axis (which makes the bone's frame non-invertible, so a
	 * later transform of a child could not be expressed at all).
	 */
	bool IsStorableBoneTransform(const FTransform& InTransform)
	{
		if (InTransform.ContainsNaN())
		{
			return false;
		}

		const FVector Scale = InTransform.GetScale3D();
		return !FMath::IsNearlyZero(Scale.X) && !FMath::IsNearlyZero(Scale.Y) && !FMath::IsNearlyZero(Scale.Z);
	}

	/**
	 * Recomputes the global transforms of InBone and everything below it, after InBone's local transform
	 * changed.
	 *
	 * Used only by the Independent nesting mode, which is the one case where a bone's parent may already have
	 * been moved by this same operation. Walking the subtree costs O(descendants) rather than recomputing the
	 * whole collection per write.
	 */
	void RefreshSubtreeGlobals(
		const FGeometryCollection& InCollection, int32 InBone, TArray<FTransform>& InOutGlobalTransforms)
	{
		if (!InOutGlobalTransforms.IsValidIndex(InBone))
		{
			return;
		}

		TArray<int32> Stack;
		Stack.Push(InBone);
		while (!Stack.IsEmpty())
		{
			const int32 Bone = Stack.Pop();

			const int32 Parent = PCGUtilsGeometryCollectionHierarchy::GetParent(InCollection, Bone);
			const FTransform ParentGlobal = InOutGlobalTransforms.IsValidIndex(Parent)
				? InOutGlobalTransforms[Parent] : FTransform::Identity;
			InOutGlobalTransforms[Bone] = FTransform(InCollection.Transform[Bone]) * ParentGlobal;

			if (InCollection.Children.IsValidIndex(Bone))
			{
				for (const int32 Child : InCollection.Children[Bone])
				{
					if (InOutGlobalTransforms.IsValidIndex(Child))
					{
						Stack.Push(Child);
					}
				}
			}
		}
	}

	/** Shared body of the two bulk entry points, working from already-resolved desired global transforms. */
	bool ApplyDesiredGlobals(
		FGeometryCollection& InOutCollection,
		TConstArrayView<int32> InBones,
		TConstArrayView<FTransform> InDesiredGlobalTransforms,
		const EPCGGeometryCollectionNestedBoneHandling InNestedHandling,
		TArray<FTransform>& InOutGlobalTransforms,
		FPCGUtilsGeometryCollectionBoneTransformResult& OutResult)
	{
		// Partition the request before touching anything: an operation that turns out to be refused must leave
		// the collection exactly as it found it.
		TSet<int32> TargetedBones;
		TargetedBones.Reserve(InBones.Num());
		for (int32 Index = 0; Index < InBones.Num(); ++Index)
		{
			const int32 Bone = InBones[Index];
			if (PCGUtilsGeometryCollectionHierarchy::IsValidBone(InOutCollection, Bone))
			{
				TargetedBones.Add(Bone);
			}
			else
			{
				OutResult.InvalidBones.Add(Bone);
			}
		}

		// Index into InBones, so each entry keeps its paired desired transform.
		TArray<int32> Applicable;
		Applicable.Reserve(InBones.Num());
		for (int32 Index = 0; Index < InBones.Num(); ++Index)
		{
			const int32 Bone = InBones[Index];
			if (!TargetedBones.Contains(Bone))
			{
				continue;
			}

			TArray<int32> Ancestors;
			PCGUtilsGeometryCollectionHierarchy::GetAncestors(InOutCollection, Bone, Ancestors);
			const bool bHasTargetedAncestor = Ancestors.ContainsByPredicate(
				[&TargetedBones](const int32 Ancestor) { return TargetedBones.Contains(Ancestor); });

			if (bHasTargetedAncestor)
			{
				OutResult.bNestedTargetsFound = true;
				if (InNestedHandling == EPCGGeometryCollectionNestedBoneHandling::Topmost)
				{
					OutResult.NestedBonesDropped.Add(Bone);
					continue;
				}
			}

			Applicable.Add(Index);
		}

		if (OutResult.bNestedTargetsFound && InNestedHandling == EPCGGeometryCollectionNestedBoneHandling::Error)
		{
			return false;
		}

		const bool bIndependent = (InNestedHandling == EPCGGeometryCollectionNestedBoneHandling::Independent);
		if (bIndependent && OutResult.bNestedTargetsFound)
		{
			// Nearest the root first, so a bone's parent has already reached its final transform by the time
			// the bone is written and the parent global read below is the one that will actually apply.
			Applicable.Sort([&InOutCollection, &InBones](const int32 LeftIndex, const int32 RightIndex)
			{
				const int32 LeftLevel =
					PCGUtilsGeometryCollectionHierarchy::GetLevel(InOutCollection, InBones[LeftIndex]);
				const int32 RightLevel =
					PCGUtilsGeometryCollectionHierarchy::GetLevel(InOutCollection, InBones[RightIndex]);
				return LeftLevel != RightLevel ? LeftLevel < RightLevel : InBones[LeftIndex] < InBones[RightIndex];
			});
		}

		for (const int32 Index : Applicable)
		{
			const int32 Bone = InBones[Index];
			const FTransform& Desired = InDesiredGlobalTransforms[Index];

			if (!PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(
				InOutCollection, Bone, InOutGlobalTransforms, Desired))
			{
				OutResult.UnrepresentableBones.Add(Bone);
				continue;
			}

			++OutResult.NumApplied;

			if (bIndependent && OutResult.bNestedTargetsFound)
			{
				// Only this mode can have a later target underneath an earlier one; every other mode leaves an
				// antichain, where no target's move can change another target's parent.
				RefreshSubtreeGlobals(InOutCollection, Bone, InOutGlobalTransforms);
			}
		}

		return true;
	}
}

namespace PCGUtilsGeometryCollectionTransforms
{
	FTransform ComputeBonePointTransform(
		const FGeometryCollection& InCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput)
	{
		const FTransform BoneToOutput =
			ComputeBoneOriginTransform(InBoneIndex, InGlobalTransforms, InCollectionToOutput);

		const FBox LocalBounds =
			PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(InCollection, InBoneIndex);
		const FVector LocalCenter = LocalBounds.IsValid ? LocalBounds.GetCenter() : FVector::ZeroVector;

		// Keep the bone's orientation and scale: the point represents the piece, not just where it is. Only the
		// translation moves to the piece's centre. Must stay identical to what PCGUtilsGeometryCollectionBonePoints
		// writes - that is the contract this whole file exists to keep.
		return FTransform(
			BoneToOutput.GetRotation(),
			BoneToOutput.TransformPosition(LocalCenter),
			BoneToOutput.GetScale3D());
	}

	FTransform ComputeBoneOriginTransform(
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput)
	{
		const FTransform BoneToCollection = InGlobalTransforms.IsValidIndex(InBoneIndex)
			? InGlobalTransforms[InBoneIndex] : FTransform::Identity;
		return BoneToCollection * InCollectionToOutput;
	}

	FTransform GetParentGlobalTransform(
		const FGeometryCollection& InCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms)
	{
		const int32 Parent = PCGUtilsGeometryCollectionHierarchy::GetParent(InCollection, InBoneIndex);
		return InGlobalTransforms.IsValidIndex(Parent) ? InGlobalTransforms[Parent] : FTransform::Identity;
	}

	bool SetBoneGlobalTransform(
		FGeometryCollection& InOutCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InDesiredGlobalTransform)
	{
		if (!PCGUtilsGeometryCollectionHierarchy::IsValidBone(InOutCollection, InBoneIndex)
			|| !InOutCollection.Transform.IsValidIndex(InBoneIndex)
			|| !IsStorableBoneTransform(InDesiredGlobalTransform))
		{
			return false;
		}

		const FTransform ParentGlobal =
			GetParentGlobalTransform(InOutCollection, InBoneIndex, InGlobalTransforms);
		if (!IsStorableBoneTransform(ParentGlobal))
		{
			return false;
		}

		// GetRelativeTransform(X) is `this * X^-1`, which inverts Global = Local * ParentGlobal exactly. Note
		// this is the one operation that can be lossy: a non-uniform parent scale combined with a rotated child
		// produces shear, which FTransform cannot represent. Callers that let scale through say so explicitly.
		const FTransform NewLocal = InDesiredGlobalTransform.GetRelativeTransform(ParentGlobal);
		if (!IsStorableBoneTransform(NewLocal))
		{
			return false;
		}

		// The collection stores float transforms; everything above composed in double, and this is the single
		// narrowing point. Mirrors PCGUtilsGeometryCollectionHelpers::PlaceCollection.
		InOutCollection.Transform[InBoneIndex] = FTransform3f(NewLocal);
		return true;
	}

	bool ApplyBoneDelta(
		FGeometryCollection& InOutCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InDeltaInCollectionSpace)
	{
		if (!InGlobalTransforms.IsValidIndex(InBoneIndex))
		{
			return false;
		}

		// Global'(b) = Global(b) * Delta. The delta composes on the right because it acts in collection space,
		// i.e. after the bone's own frame rather than inside it.
		const FTransform DesiredGlobal = InGlobalTransforms[InBoneIndex] * InDeltaInCollectionSpace;
		return SetBoneGlobalTransform(InOutCollection, InBoneIndex, InGlobalTransforms, DesiredGlobal);
	}

	void ReduceToAntichain(
		const FGeometryCollection& InCollection, TArray<int32>& InOutBones, TArray<int32>* OutDropped)
	{
		if (OutDropped)
		{
			OutDropped->Reset();
		}

		TSet<int32> Candidates;
		Candidates.Reserve(InOutBones.Num());
		for (const int32 Bone : InOutBones)
		{
			if (PCGUtilsGeometryCollectionHierarchy::IsValidBone(InCollection, Bone))
			{
				Candidates.Add(Bone);
			}
		}

		TArray<int32> Kept;
		Kept.Reserve(Candidates.Num());
		TArray<int32> Ancestors;
		for (const int32 Bone : Candidates)
		{
			PCGUtilsGeometryCollectionHierarchy::GetAncestors(InCollection, Bone, Ancestors);
			const bool bHasCandidateAncestor = Ancestors.ContainsByPredicate(
				[&Candidates](const int32 Ancestor) { return Candidates.Contains(Ancestor); });

			if (bHasCandidateAncestor)
			{
				if (OutDropped)
				{
					OutDropped->Add(Bone);
				}
			}
			else
			{
				Kept.Add(Bone);
			}
		}

		// Sorted so the result never depends on set iteration order, which is what makes the operation
		// reproducible across runs and comparable in a test.
		Kept.Sort();
		if (OutDropped)
		{
			OutDropped->Sort();
		}
		InOutBones = MoveTemp(Kept);
	}

	bool SetBoneGlobalTransforms(
		FGeometryCollection& InOutCollection,
		const TConstArrayView<int32> InBones,
		const TConstArrayView<FTransform> InDesiredGlobalTransforms,
		const EPCGGeometryCollectionNestedBoneHandling InNestedHandling,
		FPCGUtilsGeometryCollectionBoneTransformResult& OutResult)
	{
		OutResult = FPCGUtilsGeometryCollectionBoneTransformResult();
		if (InBones.Num() != InDesiredGlobalTransforms.Num())
		{
			return false;
		}

		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(InOutCollection, GlobalTransforms);

		return ApplyDesiredGlobals(
			InOutCollection, InBones, InDesiredGlobalTransforms, InNestedHandling, GlobalTransforms, OutResult);
	}

	bool ApplyBoneDeltas(
		FGeometryCollection& InOutCollection,
		const TConstArrayView<int32> InBones,
		const TConstArrayView<FTransform> InDeltasInCollectionSpace,
		const EPCGGeometryCollectionNestedBoneHandling InNestedHandling,
		FPCGUtilsGeometryCollectionBoneTransformResult& OutResult)
	{
		OutResult = FPCGUtilsGeometryCollectionBoneTransformResult();
		if (InBones.Num() != InDeltasInCollectionSpace.Num())
		{
			return false;
		}

		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(InOutCollection, GlobalTransforms);

		// Resolve every delta against the transforms as they are on entry. Doing it up front is what makes the
		// result independent of processing order: under Independent a descendant's target is still measured
		// from where it started, not from wherever its ancestor has since carried it.
		TArray<FTransform> DesiredGlobals;
		DesiredGlobals.Reserve(InBones.Num());
		for (int32 Index = 0; Index < InBones.Num(); ++Index)
		{
			const int32 Bone = InBones[Index];
			const FTransform BoneGlobal = GlobalTransforms.IsValidIndex(Bone)
				? GlobalTransforms[Bone] : FTransform::Identity;
			DesiredGlobals.Add(BoneGlobal * InDeltasInCollectionSpace[Index]);
		}

		return ApplyDesiredGlobals(
			InOutCollection, InBones, DesiredGlobals, InNestedHandling, GlobalTransforms, OutResult);
	}
}
