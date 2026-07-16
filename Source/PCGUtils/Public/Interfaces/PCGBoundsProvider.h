#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "PCGBoundsProvider.generated.h"

class AActor;

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGBoundsProvider : public UInterface
{
	GENERATED_BODY()
};

/** Provides bounds expressed relative to the supplied actor. */
class PCGUTILS_API IPCGBoundsProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Bounds")
	bool GetPCGActorBoundingBox(AActor* Actor, FBox& OutBounds) const;
};
