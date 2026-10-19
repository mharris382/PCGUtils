// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Image/PCGUtilsGreyscaleImage.h"
#include "Vectorization/PCGUtilsVectorizationTypes.h"

/**
 * Image vectorization: greyscale image in, closed boundary paths out, in image pixel coordinates.
 *
 * Every algorithm treats the image as if surrounded by background, so a shape touching the edge is still closed by
 * the image border. Every algorithm finishes the same way: paths enclosing less than MinArea are dropped, paths are
 * nested by containment (ParentIndex, Depth, bIsHole), outer boundaries are wound with positive signed area and holes
 * the other way, and Outer Only retrieval keeps depth 0. The output is ordered outer boundaries first, by depth, then
 * by area, largest first.
 *
 * All functions are pure and thread-safe.
 */
namespace PCGUtilsVectorization
{
	PCGUTILSTEXTURES_API void MarchingSquares(
		const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsMarchingSquaresOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths);

	PCGUTILSTEXTURES_API void BorderFollowing(
		const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsBorderFollowingOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths);

	PCGUTILSTEXTURES_API void CurveFit(
		const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsCurveFitOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths);

	/** Shoelace signed area of a closed polygon. Positive is counter-clockwise in a Y-up frame (clockwise on screen). */
	PCGUTILSTEXTURES_API double SignedArea(TConstArrayView<FVector2D> Points);

	/** Douglas-Peucker simplification. A closed polygon is split at its two mutually farthest points first. */
	PCGUTILSTEXTURES_API void SimplifyDouglasPeucker(TArray<FVector2D>& Points, double Epsilon, bool bClosed);

	/** Drops speckles, computes nesting and winding, applies retrieval, and sorts. Shared by every algorithm. */
	PCGUTILSTEXTURES_API void FinalizePaths(TArray<FPCGUtilsVectorPath>& Paths, double MinArea, EPCGUtilsContourRetrieval Retrieval);
}
