// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsGeometryCollectionTransforms.generated.h"

class FGeometryCollection;

/**
 * What to do when an operation targets a bone and one of its ancestors at the same time.
 *
 * Writing a bone's local transform moves its entire subtree, because children are stored parent-relative.
 * That is exactly what makes a cluster move as a unit, and it is also what makes an ancestor/descendant pair
 * ambiguous - so the ambiguity is resolved explicitly rather than by whichever bone the loop reached last.
 */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionNestedBoneHandling : uint8
{
	/**
	 * Drop the descendant and let the ancestor's move carry it.
	 *
	 * The right default: a user who selected a cluster and the pieces inside it almost always meant "move this
	 * cluster", and this is the only mode whose result does not depend on the order bones were listed in.
	 */
	Topmost,

	/**
	 * Honour every target: each bone ends at the transform asked for it, regardless of what its ancestors did.
	 *
	 * Bones are processed nearest-the-root first and each one's parent transform is re-resolved as the walk
	 * descends, so a descendant's requested transform is achieved exactly rather than composed on top of its
	 * ancestor's move.
	 */
	Independent,

	/** Refuse the whole operation and report the nesting, for a caller that wants the guarantee. */
	Error
};

/** What a bulk bone-transform operation did, so the caller can report it without re-deriving anything. */
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionBoneTransformResult
{
	/** Bones whose local transform was actually written. */
	int32 NumApplied = 0;

	/** Requested bones that are not valid transform indices in this collection. */
	TArray<int32> InvalidBones;

	/** Requested bones skipped because an ancestor was also targeted (Topmost only). */
	TArray<int32> NestedBonesDropped;

	/** A requested transform that could not be represented (non-finite, or a degenerate parent). */
	TArray<int32> UnrepresentableBones;

	/** True when any target had a targeted ancestor, whether or not it was dropped. */
	bool bNestedTargetsFound = false;
};

/**
 * All Geometry Collection bone-transform arithmetic, in one place.
 *
 * Stored bone transforms are **parent-relative** and **single precision** (`TManagedArray<FTransform3f>`), and
 * global composition is `Global(b) = Local(b) * Global(Parent(b))` in UE's row-vector convention. Every
 * function here takes and returns collection-space (global) transforms and does the conversion itself, so no
 * caller has to remember which side the parent multiplies on. Composition is always done in double precision
 * and narrowed exactly once at the write, matching PCGUtilsGeometryCollectionHelpers::PlaceCollection.
 *
 * **Why not FCollectionTransformFacade::Transform.** The engine's helper applies one shared transform to a
 * whole selection as `Transforms[Idx] = Transforms[Idx] * T`. That composes T between the bone's local
 * transform and its parent, i.e. it operates in *parent* space, so it only coincides with a collection-space
 * transform when the targeted bone's parent is at identity. A PCGUtils collection usually has one identity-ish
 * cluster root, which makes level-1 bones accidentally correct and everything deeper silently wrong - a
 * multi-level fracture, an imported asset, or a collection placed with PlaceCollection all break it. It also
 * cannot express a different transform per bone, which is the whole point of the point-driven workflow. Direct
 * managed-array manipulation behind this namespace is therefore deliberate, not an oversight.
 *
 * Nothing here publishes, copies or normalises a collection. A caller mutates a private
 * `CreateMutableCopy()` and publishes the result with `bTransformsChanged` set; the publisher owns the rest.
 */
namespace PCGUtilsGeometryCollectionTransforms
{
	/**
	 * The transform `GC | Bones To Points` emits for a bone: the bone's orientation and scale, with the
	 * translation moved to the centre of the piece's bounds.
	 *
	 * **This is the shared definition of the point pivot**, and the reason it lives here rather than in the
	 * conversion: the apply side has to reconstruct the exact same reference transform to recover what the user
	 * changed. A point sits at the piece's *bounds centre*, not at the bone's origin, so treating an incoming
	 * point transform as the desired bone transform would shift every piece by its own pivot offset. Producer
	 * and consumer calling one function is what makes that impossible to get subtly wrong.
	 *
	 * Bones with no geometry (clusters, roots) have no bounds, so the centre is the bone origin and this
	 * degenerates to the bone's own global transform composed with InCollectionToOutput.
	 *
	 * @param InGlobalTransforms    Collection-space bone matrices, from
	 *                              PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms.
	 * @param InCollectionToOutput  Collection space -> the space the point is expressed in. Identity leaves the
	 *                              result in collection space; pass the target actor transform for world space.
	 */
	PCGUTILSFRACTURE_API FTransform ComputeBonePointTransform(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput);

	/**
	 * The bone's own global transform composed with InCollectionToOutput - i.e. the same thing
	 * ComputeBonePointTransform returns but pivoted at the bone origin rather than the piece centre.
	 *
	 * This is the other half of the Point Pivot choice, for points a user authored by hand rather than through
	 * GC | Bones To Points.
	 */
	PCGUTILSFRACTURE_API FTransform ComputeBoneOriginTransform(
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput);

	/** Global(Parent(InBoneIndex)), or identity at a root or when the parent is not resolvable. */
	PCGUTILSFRACTURE_API FTransform GetParentGlobalTransform(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms);

	/**
	 * Sets one bone's collection-space transform: `Local' = DesiredGlobal * Global(Parent)^-1`.
	 *
	 * The bone's descendants keep their stored local transforms and therefore move rigidly with it, preserving
	 * every internal relationship exactly. That is the whole mechanism behind cluster-level transformation, and
	 * it is why this function deliberately does not touch a single descendant.
	 *
	 * @return false if the bone index is invalid or the requested transform is not finite; the collection is
	 *         then untouched.
	 */
	PCGUTILSFRACTURE_API bool SetBoneGlobalTransform(
		FGeometryCollection& InOutCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InDesiredGlobalTransform);

	/**
	 * Applies a collection-space delta to one bone: `Global'(b) = Global(b) * Delta`.
	 *
	 * Equivalent to the conjugated local form `Local' = Local * (Global(Parent) * Delta * Global(Parent)^-1)`,
	 * and implemented as the composition above because there is one fewer place for the parent to end up on
	 * the wrong side.
	 *
	 * @return false if the bone index is invalid or the result is not finite.
	 */
	PCGUTILSFRACTURE_API bool ApplyBoneDelta(
		FGeometryCollection& InOutCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InDeltaInCollectionSpace);

	/**
	 * Drops every bone that has an ancestor in the same set, leaving a non-nested antichain.
	 *
	 * Also removes duplicates and sorts ascending, so the result is independent of the order bones arrived in.
	 *
	 * @param OutDropped  Optional: the bones removed because an ancestor was present. Duplicates and invalid
	 *                    indices are not reported here.
	 */
	PCGUTILSFRACTURE_API void ReduceToAntichain(
		const FGeometryCollection& InCollection,
		TArray<int32>& InOutBones,
		TArray<int32>* OutDropped = nullptr);

	/**
	 * Sets the collection-space transform of several bones at once, resolving ancestor/descendant overlap
	 * according to InNestedHandling.
	 *
	 * InBones and InDesiredGlobalTransforms are parallel and must be the same length. Duplicate bone indices
	 * are *not* resolved here - a caller that can produce them must decide what a duplicate means first.
	 *
	 * Under `Independent`, bones are applied nearest-the-root first and each bone's parent global is
	 * re-resolved from the transforms already written, so a descendant reaches exactly the transform asked for
	 * it instead of that transform composed on top of its ancestor's move.
	 *
	 * @return false only when the operation was refused outright (mismatched array lengths, or nesting under
	 *         `Error`). Individual bad bones are reported through OutResult and skipped.
	 */
	PCGUTILSFRACTURE_API bool SetBoneGlobalTransforms(
		FGeometryCollection& InOutCollection,
		TConstArrayView<int32> InBones,
		TConstArrayView<FTransform> InDesiredGlobalTransforms,
		EPCGGeometryCollectionNestedBoneHandling InNestedHandling,
		FPCGUtilsGeometryCollectionBoneTransformResult& OutResult);

	/**
	 * Applies a per-bone collection-space delta to several bones at once.
	 *
	 * Every delta is resolved against the collection's transforms *as they were on entry*, so the result does
	 * not depend on the order the bones are listed in even under `Independent`.
	 *
	 * @return false under the same conditions as SetBoneGlobalTransforms.
	 */
	PCGUTILSFRACTURE_API bool ApplyBoneDeltas(
		FGeometryCollection& InOutCollection,
		TConstArrayView<int32> InBones,
		TConstArrayView<FTransform> InDeltasInCollectionSpace,
		EPCGGeometryCollectionNestedBoneHandling InNestedHandling,
		FPCGUtilsGeometryCollectionBoneTransformResult& OutResult);
}
