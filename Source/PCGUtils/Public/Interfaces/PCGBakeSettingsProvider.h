#pragma once

#include "CoreMinimal.h"
#include "Data/PCGBakeSettings.h"
#include "UObject/Interface.h"

#include "PCGBakeSettingsProvider.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGBakeSettingsProvider : public UInterface
{
	GENERATED_BODY()
};

class PCGUTILS_API IPCGBakeSettingsProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Bake")
	FPCGUtilsBakeSettings GetPCGBakeSettings() const;
};
