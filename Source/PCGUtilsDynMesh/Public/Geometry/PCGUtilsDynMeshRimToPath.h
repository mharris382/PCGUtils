// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/**
 * Fitting a closed mesh rim to a closed target path whose points have nothing to do with the rim's vertices: a
 * different count, a different start, either winding. This is what lets DynMesh | Extrude To Paths take the output
 * of any path operation (Clipper2 offsets and booleans, smoothing, resampling) instead of only paths that still
 * carry one point per rim vertex.
 *
 * The pairing is the classic contour-stitching problem. Rim and target are walked round together, each rim vertex
 * taking a target corner and never going backwards, minimising the total distance between partners. A strip of
 * triangles then joins each rim edge to the run of target corners between its two ends, fanning from whichever
 * rim vertex is nearer. Every target corner ends up a vertex of the result, so the shape is reproduced exactly.
 *
 * Like PCGUtilsDynMeshLoft, this layer knows nothing about PCG and only needs GeometryCore. Positions are in
 * whatever space the caller uses; the element supplies mesh-local ones.
 */
namespace PCGUtilsDynMeshRimToPath
{
	/** Area-weighted normal of a closed polyline (Newell's method), not normalised. Zero for a degenerate loop. */
	PCGUTILSDYNMESH_API FVector3d LoopNormal(TConstArrayView<FVector3d> Loop);

	/** Arc length round a closed loop, closing segment included. */
	PCGUTILSDYNMESH_API double LoopLength(TConstArrayView<FVector3d> Loop);

	/** Removes points within Tolerance of the point before them, and a last point repeating the first. */
	PCGUTILSDYNMESH_API void CleanLoop(TArray<FVector3d>& InOutLoop, double Tolerance);

	/**
	 * How far apart two closed loops are as shapes: the mean distance from each loop's points to the other loop,
	 * averaged both ways. Point counts and starting points do not matter.
	 */
	PCGUTILSDYNMESH_API double ShapeDistance(TConstArrayView<FVector3d> A, TConstArrayView<FVector3d> B);

	/** Reverses Target if it winds the other way round from Rim, and rotates it to start at its point nearest Rim[0]. */
	PCGUTILSDYNMESH_API void AlignTargetToRim(TConstArrayView<FVector3d> Rim, TArray<FVector3d>& InOutTarget);

	/**
	 * Pairs every rim vertex with a target corner. OutCorner[0] is 0 and the sequence never decreases, so walking
	 * the rim walks the target the same way. The summed distance between partners is minimised, with a cost of one
	 * average target edge for two rim vertices sharing one corner, so they share only when the target has too few corners nearby (it
	 * has fewer corners than the rim has vertices, or none in between). Target must be aligned first.
	 */
	PCGUTILSDYNMESH_API double PairRimWithCorners(
		TConstArrayView<FVector3d> Rim, TConstArrayView<FVector3d> Target, TArray<int32>& OutCorner);

	/**
	 * AlignTargetToRim and PairRimWithCorners together, choosing the start better. The target point nearest Rim[0]
	 * is not always its partner: two points can be about as near, and starting on the wrong one shifts the whole
	 * pairing by a corner. So the few target points nearest Rim[0] are each tried as the start and the pairing
	 * with the lowest total cost is kept. InOutTarget comes back turned and rotated to that start.
	 */
	PCGUTILSDYNMESH_API void FitRimToTarget(
		TConstArrayView<FVector3d> Rim, TArray<FVector3d>& InOutTarget, TArray<int32>& OutCorner);

	/** Where the rim's own vertices go when they are kept (Keep Faces) rather than replaced. */
	struct FInnerRimPlan
	{
		/**
		 * Per rim vertex: its position on the target. A vertex that has a corner to itself sits on that corner;
		 * vertices sharing a corner are spread along the target beside it, so no two coincide. All lie on the
		 * target, in order round it.
		 */
		TArray<FVector3d> RimPositions;

		/** Per rim vertex: the target corners lying between it and the next rim vertex, in order. */
		TArray<TArray<int32>> CornersAfter;
	};

	PCGUTILSDYNMESH_API void PlanInnerRim(TConstArrayView<FVector3d> Rim, TConstArrayView<FVector3d> Target,
		TConstArrayView<int32> Corner, FInnerRimPlan& OutPlan);

	/**
	 * Triangulates one strip segment: rim edge A-B joined to an inner chain that runs from A's partner to B's.
	 * Returns K: chain points 0..K fan from A, K..last fan from B, and triangle (A, B, Chain[K]) bridges the two.
	 * K is chosen so each chain point joins the nearer of A and B, which keeps the strip's edges short.
	 */
	PCGUTILSDYNMESH_API int32 ChooseFanSplit(const FVector3d& A, const FVector3d& B, TConstArrayView<FVector3d> Chain);
}
