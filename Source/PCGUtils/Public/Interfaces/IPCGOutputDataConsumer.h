#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"


#include "IPCGOutputDataConsumer.generated.h"



UINTERFACE(MinimalAPI, BlueprintType)
class UPCGOutputDataConsumer : public UInterface
{
    GENERATED_BODY()
};


class PCGUTILS_API IPCGOutputDataConsumer
{
    GENERATED_BODY()

public:

    //list of pin names that this consumer wants to recieve.  If no target pins are specified, consumer will recieve all output data from the PCG Graph
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCGUtils|OutputData")
    TArray<FString> GetTargetDataPins();
   virtual TArray<FString> GetTargetDataPins_Implementation() { return TArray<FString>();}


    

    
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCGUtils|OutputData")
    void OnPCGGeneratedDataFound(bool bIsConstructor);
    virtual void OnPCGGeneratedDataFound_Implementation(bool bIsConstructor) {}
};