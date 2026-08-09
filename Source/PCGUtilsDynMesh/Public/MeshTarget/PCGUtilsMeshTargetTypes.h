#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsMeshTargetTypes.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsSelectionBlendMode : uint8
{
	Hard,
	Feathered
};

/** Controls how a topology-preserving result is composited onto a Mesh Selection. */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsSelectionBlendOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (PCG_Overridable))
	EPCGUtilsSelectionBlendMode Mode = EPCGUtilsSelectionBlendMode::Hard;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend",
		meta = (ClampMin = "0.0", EditCondition = "Mode==EPCGUtilsSelectionBlendMode::Feathered", EditConditionHides, PCG_Overridable))
	double FeatherDistance = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend",
		meta = (ClampMin = "0.01", EditCondition = "Mode==EPCGUtilsSelectionBlendMode::Feathered", EditConditionHides, PCG_Overridable))
	double FeatherSharpness = 1.0;
};
