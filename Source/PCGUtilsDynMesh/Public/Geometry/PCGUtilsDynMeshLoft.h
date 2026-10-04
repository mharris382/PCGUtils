// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Templates/Function.h"

/**
 * Structured surface generation between two rails: a quad grid with one column per rail point and a chosen
 * number of rows across, the height of each row following a profile.
 *
 * Like PCGUtilsDynMeshSurfacePathing, this layer knows nothing about PCG - it only needs GeometryCore. The PCG
 * element (DynMesh | Loft Paths) supplies mesh-local positions; every coordinate-space decision stays there.
 *
 * ## Why this exists rather than remeshing a displaced surface
 *
 * Remesh produces an isotropic triangulation whose edges point in arbitrary directions. That is harmless on a
 * plane, but on a slope the edges need to run along and across the surface, or every crease and every curve
 * comes out faceted. A loft is structured by construction: its edges follow the rails and the profile.
 *
 * ## The seam contract
 *
 * Rail points are used verbatim. Row 0 is Rail A exactly and the last row is Rail B exactly, so a loft whose
 * rail was read off an existing mesh boundary lands its edge vertices on that boundary's vertices and can be
 * welded to it. Nothing here resamples a rail; ResampleByArcLength() is a separate, explicit step.
 */
namespace PCGUtilsDynMeshLoft
{
	struct FLoftOptions
	{
		/** Quad rows between the rails. One row is a plain ruled strip. */
		int32 NumRows = 8;

		/** The rails are closed loops: the last column connects back to the first. */
		bool bClosed = false;

		/** Direction "height" is measured along; everything perpendicular to it is interpolated linearly. */
		FVector3d UpAxis = FVector3d::UnitZ();

		/**
		 * Height blend across the loft: maps t in [0,1] (0 at Rail A, 1 at Rail B) to the fraction of the
		 * A-to-B height difference reached at t. Unset means linear. Row 0 and the last row ignore it.
		 */
		TFunction<double(double)> HeightProfile;

		/** Faces point along UpAxis by default; this reverses them. */
		bool bFlipFaces = false;

		/** UV units per world unit. U runs along Rail A's arc length, V across the loft. */
		double UVScale = 0.01;
	};

	struct FLoftResult
	{
		int32 NumColumns = 0;
		int32 NumRows = 0;

		/** Vertex IDs of row 0 and of the last row, in rail order. */
		TArray<int32> RailAVertexIDs;
		TArray<int32> RailBVertexIDs;
	};

	/**
	 * Builds a loft between two rails of equal point count into OutMesh, which is expected to be empty. Returns
	 * false and fills OutError, without touching OutMesh, when the rails cannot be lofted.
	 */
	PCGUTILSDYNMESH_API bool BuildLoft(
		TConstArrayView<FVector3d> RailA,
		TConstArrayView<FVector3d> RailB,
		const FLoftOptions& Options,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FLoftResult& OutResult,
		FString& OutError);

	struct FOffsetOptions
	{
		bool bClosed = false;
		FVector3d UpAxis = FVector3d::UnitZ();

		/** Offset perpendicular to UpAxis, along (rail direction x UpAxis). Negate it to offset to the other side. */
		double Distance = 100.0;

		/** For a closed rail, make positive Distance mean "away from the enclosed area" whatever its winding. */
		bool bPositiveIsOutward = true;

		/** Offset along UpAxis. */
		double Height = 0.0;

		/** Caps how far a corner's offset may exceed Distance, as a multiple of it. */
		double MiterLimit = 2.0;
	};

	struct FOffsetResult
	{
		/**
		 * Segments whose offset copy runs backwards: the offset is larger than the corner it wraps can absorb,
		 * so the loft's columns cross there. FirstInvertedSegment is the index of its first point in the rail.
		 */
		int32 NumInvertedSegments = 0;
		int32 FirstInvertedSegment = INDEX_NONE;
	};

	/** Offsets a rail one point for one point, mitering corners. OutRail has the same count and order as Rail. */
	PCGUTILSDYNMESH_API FOffsetResult OffsetRail(
		TConstArrayView<FVector3d> Rail, const FOffsetOptions& Options, TArray<FVector3d>& OutRail);

	/**
	 * Reorders Rail so it runs the same way as Reference and, for a closed rail, starts at the point nearest
	 * Reference's first point. Without this, pairing two independently authored rails by arc length twists the loft.
	 */
	PCGUTILSDYNMESH_API void AlignRail(
		TConstArrayView<FVector3d> Reference, bool bClosed, const FVector3d& UpAxis, TArray<FVector3d>& InOutRail);

	/** Resamples a rail to Count points evenly spaced by arc length. An open rail keeps both of its end points. */
	PCGUTILSDYNMESH_API void ResampleByArcLength(
		TConstArrayView<FVector3d> Rail, bool bClosed, int32 Count, TArray<FVector3d>& OutRail);
}
