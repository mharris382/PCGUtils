#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "PCGPoint.h"
#include "UObject/Interface.h"

#include "PCGPointProvider.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGPointProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies one or more standalone PCG points and their shared point-component data. */
class PCGUTILS_API IPCGPointProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Point")
	bool GetPCGPointData(TArray<FPCGPoint>& OutPoints, FPointComponentData& OutPointData) const;
};
