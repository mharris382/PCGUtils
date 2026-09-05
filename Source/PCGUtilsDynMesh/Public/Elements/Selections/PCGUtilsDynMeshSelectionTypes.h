// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsDynMeshSelectionTypes.generated.h"

/** Public representation produced by a unified DynMesh selection element. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionRepresentation : uint8
{
	Selector UMETA(DisplayName="Selector (Deferred)"),
	Selection UMETA(DisplayName="Selection (Materialized)")
};

/** Element domain used when a deferred Selector is materialized. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionElementType : uint8
{
	Triangle,
	Vertex,
	Edge
};
