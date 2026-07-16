#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "PCGComponentProvider.generated.h"

class UPCGComponent;

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGComponentProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies the primary PCG component used by component-triggered regeneration. */
class PCGUTILS_API IPCGComponentProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Generation")
	UPCGComponent* GetPrimaryPCGComponent() const;
	virtual UPCGComponent* GetPrimaryPCGComponent_Implementation() const { return nullptr; }

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Generation")
	bool AllowsComponentTriggeredRegeneration() const;
	virtual bool AllowsComponentTriggeredRegeneration_Implementation() const { return true; }
};
