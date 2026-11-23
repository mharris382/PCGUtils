// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Points/PCGUtilsElementPoints.h"

#include "PCGUtilsGeometryCollectionPointConversion.generated.h"

struct FPCGContext;
struct FPCGUtilsGeometryCollectionSelectionEvaluationContext;

/** What stands for a Geometry Collection bone (a piece). */
UENUM(BlueprintType)
enum class EPCGUtilsGeometryCollectionBonePoints : uint8
{
	/** One point at the centre of the piece's bounds, with no bounds. */
	Center,
	/** One point at the centre of the piece's bounds, carrying those bounds. */
	CenterWithBounds UMETA(DisplayName="Center With Bounds"),
	/** Eight points with no bounds: the corners of the piece's bounds. Match decides how many must be selected. */
	BoundsCorners UMETA(DisplayName="Bounds Corners"),
	/** One point with no bounds per vertex of the piece. Exact, and heavier. Match decides how many must be selected. */
	Vertices,
};

/** How Geometry Collection bones become points in the world, so that anything that selects points can select bones. */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionPointConversion
{
	GENERATED_BODY()

	/**
	 * What stands for a piece: the centre of its bounds, that centre with the bounds (default), the eight corners of
	 * its bounds, or every vertex of its geometry (exact, heavier).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGUtilsGeometryCollectionBonePoints BonePoints = EPCGUtilsGeometryCollectionBonePoints::CenterWithBounds;

	/**
	 * Only used where a piece stands as several points (Bounds Corners, Vertices): Any selects the piece when at
	 * least one of its points is selected, All when every one is, At Least Fraction when at least Match Fraction of
	 * them are.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGUtilsElementPointMatch Match = EPCGUtilsElementPointMatch::Any;

	/** For At Least Fraction: the share of a piece's points that must be selected, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion",
		meta=(ClampMin="0", ClampMax="1", EditCondition="Match == EPCGUtilsElementPointMatch::AtLeastFraction", EditConditionHides))
	double MatchFraction = 0.5;
};

namespace PCGUtilsGeometryCollectionElementPoints
{
	/**
	 * The GC domain's conversion: every piece with valid bounds as points in the world. Clusters are never
	 * candidates, and a piece without valid bounds is skipped. Element ids are bone indices, ascending.
	 *
	 * A collection is target-actor-local, so points are placed by the target actor transform, the same conversion
	 * GC | Bones To Points makes. Without a PCG context there is no target actor and the collection is read as
	 * already being in the world.
	 *
	 * False, with OutError naming the exact failed invariant, when Match Fraction is outside 0 to 1. Nothing is
	 * logged here.
	 */
	PCGUTILSFRACTURE_API bool BonesToPoints(
		const FPCGUtilsGeometryCollectionPointConversion& Conversion,
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& EvaluationContext,
		FPCGContext* Context,
		FPCGUtilsElementPoints& OutPoints,
		FText& OutError);
}
