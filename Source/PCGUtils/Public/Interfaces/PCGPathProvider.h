#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "PCGPoint.h"
#include "UObject/Interface.h"

#include "PCGPathProvider.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGPathProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies a PCG point path, its coordinate space, and shared path data. */
class PCGUTILS_API IPCGPathProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	TArray<FPCGPoint> GetPathPoints(bool& bIsLocalSpace) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	FPathComponentData GetPathData() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	bool GetIsClosedLoop() const;
};
