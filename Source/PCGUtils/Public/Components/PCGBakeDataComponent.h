// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OverrideGraphs.h"
#include "Components/ActorComponent.h"
#include "PCGBakeDataComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UPCGBakeDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UPCGBakeDataComponent();

	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG|Bake")
	FString BakedAssetSaveName;

	/** Output path for baked static mesh and data assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake", meta = (ContentDir))
	FDirectoryPath BakedAssetSavePath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake")
	FString BakedAssetGroupLabel = TEXT("DefaultGroup");
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake")
	FPCGOverrideGraph PreBakeGraph;
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake")
	FPCGOverrideGraph PostBakeGraph;
};
