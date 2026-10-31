// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Painters/PCGDynMeshDistancePainter.h"

namespace PCGUtilsPainterDistance
{
	/**
	 * Read-only distance field over a set of points, safe to evaluate from many threads at once.
	 *
	 * Each point covers a world box: its shape's bounds grown by Max Radius. No metric can reach farther than that
	 * along any axis, so a vertex outside the box can never be in range. The boxes are put in a uniform grid
	 * whose cell size is their mean size, so a vertex only tests the points listed in its own cell. A point
	 * whose box would cover too many cells is tested by every vertex instead.
	 */
	class FDistanceField
	{
	public:
		explicit FDistanceField(const UPCGDynMeshDistancePainterFactoryData& InFactory);

		/** Final painter value at a world position. Uses the grid. */
		float Evaluate(const FVector& WorldPosition) const;

		/** Same result as Evaluate, testing every point. For tests. */
		float EvaluateBruteForce(const FVector& WorldPosition) const;

		/** Distance from a world position to one point's shape, as the factory measures it. For tests. */
		double DistanceTo(int32 PointIndex, const FVector& WorldPosition) const;

		int32 NumGlobalPoints() const { return GlobalPoints.Num(); }
		int32 NumCells() const { return CellToBucket.Num(); }

	private:
		struct FPreparedPoint
		{
			FTransform Transform;
			FVector Location = FVector::ZeroVector;
			FVector BoundsMin = FVector::ZeroVector;
			FVector BoundsMax = FVector::ZeroVector;
			double SphereRadius = 0.0;
			FBox Influence = FBox(ForceInit);
		};

		/** Running combination of the points in range of one vertex. */
		struct FAccumulator
		{
			double Value = 0.0;
			double NearestDistance = TNumericLimits<double>::Max();
			int32 NearestIndex = INDEX_NONE;
			int32 Count = 0;
		};

		void Accumulate(int32 PointIndex, const FVector& WorldPosition, FAccumulator& InOut) const;
		float Finish(const FAccumulator& Accumulator) const;
		double Measure(const FPreparedPoint& Point, const FVector& WorldPosition) const;
		double ValueAt(double Distance) const;
		FIntVector CellOf(const FVector& WorldPosition) const;

		const UPCGDynMeshDistancePainterFactoryData& Factory;
		TArray<FPreparedPoint> Points;

		double InvCellSize = 1.0;
		TMap<FIntVector, int32> CellToBucket;
		/** Bucket B's points are BucketPoints[BucketStarts[B] .. BucketStarts[B + 1]), in input order. */
		TArray<int32> BucketStarts;
		TArray<int32> BucketPoints;
		/** Points whose influence covers too many cells; every vertex tests them. */
		TArray<int32> GlobalPoints;
	};
}
