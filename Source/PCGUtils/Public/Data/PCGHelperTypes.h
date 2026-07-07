#pragma once
#include "CoreMinimal.h"
#include "PCGGraph.h"
#include "PCGDataAsset.h"
#include "PCGHelperTypes.generated.h"







USTRUCT(BlueprintType)
struct PCGUTILS_API FTransformRandomSettings
{
	GENERATED_BODY();

	FTransformRandomSettings() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FVector OffsetMin = FVector(0.0, 0.0, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FVector OffsetMax = FVector(0.0, 0.0, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool AbsoluteOffset = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FRotator RotationMin = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FRotator RotationMax = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool AbsoluteRotation = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings", meta=(AllowPreserveRatio))
	FVector ScaleMin = FVector(1.0, 1.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings", meta=(AllowPreserveRatio))
	FVector ScaleMax = FVector(1.0, 1.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool bUniformScale = false;
	
};

