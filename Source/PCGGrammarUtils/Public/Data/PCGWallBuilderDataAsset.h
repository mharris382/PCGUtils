#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCGWallBuilderDataAsset.generated.h"

USTRUCT(BlueprintType)
struct PCGGRAMMARUTILS_API FWallModuleInfo
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Symbol;
};

UCLASS(BlueprintType)
class PCGGRAMMARUTILS_API UPCGWallBuilderDataAsset : public UDataAsset
{
    GENERATED_BODY()
};
