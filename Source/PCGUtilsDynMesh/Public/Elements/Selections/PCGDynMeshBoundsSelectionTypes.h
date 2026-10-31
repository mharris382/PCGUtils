// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGDynMeshBoundsSelectionTypes.generated.h"

UENUM(BlueprintType)
enum class EPCGDynMeshBoundsSelectionElementType : uint8
{
	Vertex,
	Edge,
	Triangle
};

UENUM(BlueprintType)
enum class EPCGDynMeshBoundsTestMode : uint8
{
	/** Selects an element if its center (edge midpoint / triangle centroid) is inside the point's bounds. */
	ElementCenterInside,
	/** Selects an element if any one of its vertices is inside the point's bounds. */
	AnyVertexInside,
	/** Selects an element if all of its vertices are inside the point's bounds. */
	AllVerticesInside
};

/** How an element lying in several points' bounds is judged when those points each test its normal. */
UENUM(BlueprintType)
enum class EPCGDynMeshBoundsNormalOverlapMode : uint8
{
	/** Selects an element if its normal passes for at least one of the bounds it lies in. */
	Any,
	/** Selects an element only if its normal passes for every one of the bounds it lies in. */
	All
};
