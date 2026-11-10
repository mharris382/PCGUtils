// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OverrideGraphs.h"
#include "UObject/ObjectMacros.h"

#include "PCGBakeSettings.generated.h"

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsBakeSettings
{
	GENERATED_BODY()
	
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
	
	bool IsValidSavePath() const;
	
	FString GetBakeAssetSaveName() const { return BakedAssetGroupLabel+ "_"+ BakedAssetSaveName; }
	FSoftObjectPath GetBakedAssetSoftPath() const;

	/**
	 * Object path of a baked asset: {BakedAssetSavePath}/{BakedAssetSaveName}{NameSuffix}. This matches what the bake
	 * subgraphs write, where BakedAssetSaveName already carries the group label. Backslashes and a trailing slash in
	 * the save path are tolerated. Returns a null path when the save name or the save path is empty.
	 */
	FSoftObjectPath GetBakedAssetObjectPath(const FString& NameSuffix = FString()) const;
};

