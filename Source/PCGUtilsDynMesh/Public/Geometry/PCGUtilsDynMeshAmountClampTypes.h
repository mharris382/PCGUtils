// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsDynMeshAmountClampTypes.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshAmountClampMode : uint8
{
	Regional UMETA(DisplayName="Per Vertex", ToolTip="Reduce only conflicting bevel endpoints. No region size or propagation; the other endpoint can retain its full amount."),
	SelectionIsland UMETA(DisplayName="Selection Island", ToolTip="Use one amount for each connected island of the selection."),
	WholeMesh UMETA(DisplayName="Whole Mesh", ToolTip="Use the smallest safe amount for the complete mesh selection.")
};
