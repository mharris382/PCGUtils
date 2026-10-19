// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Image/PCGUtilsGreyscaleImage.h"
#include "Vectorization/PCGUtilsVectorizationTypes.h"

/** Steps shared between the vectorizers that are not part of the public API. */
namespace PCGUtilsVectorization::Internal
{
	/**
	 * FinalizePaths, optionally trusting the ParentIndex the algorithm already worked out (border following knows
	 * its hierarchy exactly). Dropped paths hand their children to their nearest kept ancestor either way.
	 */
	void FinalizePaths(TArray<FPCGUtilsVectorPath>& Paths, double MinArea, EPCGUtilsContourRetrieval Retrieval, bool bHierarchyKnown);

	/** The raw closed iso-contours at Level, one point per crossed pixel edge, before any simplification. */
	void TraceIsoContours(
		const FPCGUtilsGreyscaleImage& Image, float Level, bool bInterpolate, EPCGUtilsMarchingSquaresSaddle Saddle,
		TArray<TArray<FVector2D>>& OutLoops);
}
