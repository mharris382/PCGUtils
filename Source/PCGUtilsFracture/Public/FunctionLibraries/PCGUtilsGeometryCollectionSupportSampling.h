// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsGeometryCollectionSupportSampling.generated.h"

class FGeometryCollection;

/**
 * How much of a piece's shape a projection takes into account.
 *
 * The tiers differ only in which points get traced; the solve is identical for all of them. That is deliberate -
 * it is what lets a new tier be added without touching the solver, and what would let a future coarse-to-fine
 * pass re-run one bone at a finer tier without special-casing anything.
 */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionProjectionAccuracy : uint8
{
	/**
	 * One sample, at the leading point of the unit's bounds.
	 *
	 * A single trace per unit. Right for small rubble and for iterating on a graph with thousands of fragments;
	 * wrong for anything long or irregular, which will rest on one point and hang in the air at the other end.
	 */
	Pivot,

	/**
	 * The leading corners of each piece's oriented bounds.
	 *
	 * Four traces per piece. Conservative by construction - a piece can float slightly where its bounds are much
	 * larger than its geometry, but it never sinks into the surface. The right default.
	 */
	Bounds
};

/**
 * Produces the points a projection traces for one bone.
 *
 * A bone's samples come from the pieces *beneath* it, each under its own global transform, never from the bone's
 * own geometry: a cluster's own geometry is the hidden pre-fracture shape the revision publisher normally
 * removes, and treating it as the cluster's silhouette would project against the unfractured solid. This is what
 * makes one transform for a whole cluster work out to the same answer as projecting its silhouette.
 */
namespace PCGUtilsGeometryCollectionSupportSampling
{
	struct FSamplingSettings
	{
		EPCGGeometryCollectionProjectionAccuracy Accuracy = EPCGGeometryCollectionProjectionAccuracy::Bounds;

		/** Unit vector the projection travels along, in the same space as InCollectionToOutput's output. */
		FVector Direction = -FVector::UpVector;
	};

	/**
	 * Appends the support samples for one bone.
	 *
	 * @param InGlobalTransforms    Collection-space bone matrices.
	 * @param InCollectionToOutput  Collection space -> the space the samples (and the projection direction) are
	 *                              expressed in.
	 * @param OutSamples            Appended to, not reset, so a caller can gather several bones into one buffer.
	 * @return number of samples appended. Zero when the bone has no geometry beneath it, which a caller should
	 *         treat as "nothing to project" rather than as an error.
	 */
	PCGUTILSFRACTURE_API int32 GatherSupportSamples(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput,
		const FSamplingSettings& InSettings,
		TArray<FVector>& OutSamples);

	/**
	 * The bounds of everything beneath a bone, in the output space.
	 *
	 * Exposed because a caller needs it to size a sensible trace start-off distance, and because it is the same
	 * union the Pivot tier samples from. Returns an invalid box when the bone has no geometry beneath it.
	 */
	PCGUTILSFRACTURE_API FBox ComputeBoneWorldBounds(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput);
}
