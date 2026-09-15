// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/**
 * Turns a set of contact samples into the rigid transform that settles a body against whatever they hit.
 *
 * Deliberately knows nothing about Geometry Collections, PCG, meshes or the world. It takes points that have
 * already been traced and returns a transform; producing the samples and tracing them belong to the caller. That
 * is what lets the same solver serve a Geometry Collection piece, a whole cluster, and an automated test running
 * against an analytic plane, with no shared machinery beyond this header.
 *
 * ## The one non-obvious rule
 *
 * A body settles at the **minimum** travel over its samples, not the maximum. Each sample reports how far *it*
 * could travel before hitting something; the body stops as soon as the first one lands. Taking the maximum would
 * sink a slab until its highest corner touched. Negative travel is meaningful and is kept: a fragment that
 * starts buried is pushed back out, which is exactly what a wall section half inside a dune needs.
 */
namespace PCGUtilsProjectionSolver
{
	/** One point that could make contact, plus what it hit. All in one caller-chosen space. */
	struct FSupportSample
	{
		/** Where the sample started, before projection. */
		FVector Position = FVector::ZeroVector;

		/** Where its trace landed. Only meaningful when bHit. */
		FVector HitLocation = FVector::ZeroVector;

		/** Surface normal at the hit. Only meaningful when bHit. Unused until rotation support lands. */
		FVector HitNormal = FVector::UpVector;

		/** Signed distance along the projection direction from Position to HitLocation. Negative means buried. */
		double Travel = 0.0;

		/** False when the trace found nothing within range; the sample is then ignored by the solve. */
		bool bHit = false;
	};

	struct FSolveSettings
	{
		/** Unit vector the body travels along. Not normalised for you - Solve assumes it already is. */
		FVector Direction = -FVector::UpVector;
	};

	struct FSolveResult
	{
		/** The transform to compose onto the body, in the same space the samples were given in. */
		FTransform Delta = FTransform::Identity;

		/** Samples that hit. Zero means nothing was found and Delta is identity. */
		int32 NumHits = 0;

		/** The travel actually applied: the minimum over the samples that hit. */
		double Travel = 0.0;

		/**
		 * Spread of travel across the samples that hit, i.e. how badly the body fits what it landed on.
		 *
		 * Zero on a flat surface under a flat body; large where the samples disagree, which is the signal a
		 * coarse-to-fine pass would use to decide a cluster should be split and its pieces projected
		 * individually. Nothing consumes it yet - it is populated because it costs one subtraction and because
		 * adding it later would change this struct out from under callers.
		 */
		double SupportSpread = 0.0;

		/** False when no sample hit. The caller should leave the body alone rather than apply an identity. */
		bool bSolved = false;
	};

	/**
	 * Solves the translation that rests the sampled body against its contacts.
	 *
	 * Rotation is not yet solved; Delta is always a pure translation. The result struct and this signature are
	 * already shaped for it, so adding surface alignment does not disturb either.
	 */
	PCGUTILSDYNMESH_API FSolveResult Solve(
		TConstArrayView<FSupportSample> InSamples, const FSolveSettings& InSettings);
}
