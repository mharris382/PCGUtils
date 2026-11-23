// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Points/PCGUtilsElementPoints.h"

#include "PCGUtilsDynMeshPointConversion.generated.h"

struct FPCGContext;
struct FPCGUtilsDynMeshSelectionEvaluationContext;

/** What stands for a mesh edge. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshEdgePoints : uint8
{
	/** One point at the edge's midpoint with no bounds. */
	Midpoint,
	/** One point at the edge's midpoint whose bounds are the box round the edge's two ends. */
	MidpointWithBounds UMETA(DisplayName="Midpoint With Bounds"),
	/** Two points with no bounds: the edge's two vertices. Match decides how many must be selected. */
	Vertices,
};

/** What stands for a mesh triangle. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshTrianglePoints : uint8
{
	/** One point at the triangle's centre with no bounds. */
	Center,
	/** One point at the triangle's centre whose bounds are the box round its three corners. */
	CenterWithBounds UMETA(DisplayName="Center With Bounds"),
	/** Three points with no bounds: the triangle's vertices. Match decides how many must be selected. */
	Vertices,
};

/**
 * How DynMesh elements become points in the world, so that anything that selects points can select mesh elements.
 * A vertex is always one point at the vertex, with no bounds.
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshPointConversion
{
	GENERATED_BODY()

	/** What stands for an edge when edges are selected: its midpoint, its midpoint with the box round it (default), or its two vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGUtilsDynMeshEdgePoints EdgePoints = EPCGUtilsDynMeshEdgePoints::MidpointWithBounds;

	/** What stands for a triangle when faces are selected: its centre, its centre with the box round it (default), or its three vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGUtilsDynMeshTrianglePoints TrianglePoints = EPCGUtilsDynMeshTrianglePoints::CenterWithBounds;

	/**
	 * Only used where an element stands as several points (Vertices): Any selects the element when at least one of
	 * its vertices is selected, All when every one is, At Least Fraction when at least Match Fraction of them are.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGUtilsElementPointMatch Match = EPCGUtilsElementPointMatch::Any;

	/** For At Least Fraction: the share of an element's points that must be selected, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion",
		meta=(ClampMin="0", ClampMax="1", EditCondition="Match == EPCGUtilsElementPointMatch::AtLeastFraction", EditConditionHides))
	double MatchFraction = 0.5;
};

namespace PCGUtilsDynMeshElementPoints
{
	/**
	 * The DynMesh domain's conversion: every element of the evaluation's domain (vertices, edges or triangles of a
	 * triangle mesh) as points in the world. Element ids are the mesh's own vertex, edge or triangle ids.
	 *
	 * DynMesh data is target-actor-local, so points are placed by the mesh's target actor transform (the one
	 * PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform gives). Without a PCG context there is no target actor
	 * and the mesh is read as already being in the world.
	 *
	 * False, with OutError naming the exact failed invariant, when the domain is not supported (polygroup
	 * topology) or Match Fraction is outside 0 to 1. Nothing is logged here.
	 */
	PCGUTILSDYNMESH_API bool ElementsToPoints(
		const FPCGUtilsDynMeshPointConversion& Conversion,
		const FPCGUtilsDynMeshSelectionEvaluationContext& EvaluationContext,
		FPCGContext* Context,
		FPCGUtilsElementPoints& OutPoints,
		FText& OutError);
}
