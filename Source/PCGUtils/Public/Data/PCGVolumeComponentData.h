// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGVolumeComponentData.generated.h"

/** Authored values shared by a volume's spatial and DynMesh outputs. */
USTRUCT(BlueprintType)
struct PCGUTILS_API FVolumeComponentData
{
	GENERATED_BODY()

	/** General-purpose ID for grouping or identifying volumes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG")
	int32 VolumeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta=(ClampMin="0", ClampMax="1", UIMin="0", UIMax="1"))
	float VolumeDensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG")
	FLinearColor VolumeColor = FLinearColor::White;
};
