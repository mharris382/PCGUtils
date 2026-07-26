#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCGData.h"
#include "PCGUtilsDataCacheTypes.h"
#include "PCGUtilsDataCacheBlueprintLibrary.generated.h"

class UPCGDataAsset;
class UPCGDataCacheComponent;

/** Runtime-safe Blueprint access to PCG data-cache references and their stored collections. */
UCLASS()
class PCGUTILSDATACACHE_API UPCGUtilsDataCacheBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache", meta=(DefaultToSelf="Actor"))
	static UPCGDataCacheComponent* GetDataCacheComponent(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	static bool GetCacheAssetPathFromActor(const AActor* Actor, EPCGUtilsDataCache TargetCache,
		FSoftObjectPath& OutAssetPath);

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	static bool DoesCacheDataExist(const UPCGDataCacheComponent* CacheComponent,
		EPCGUtilsDataCache TargetCache = EPCGUtilsDataCache::Runtime);

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	static bool DoesActorCacheDataExist(const AActor* Actor,
		EPCGUtilsDataCache TargetCache = EPCGUtilsDataCache::Runtime);

	/** Synchronously loads the referenced cache asset. Intended for small, gameplay-critical cache data. */
	UFUNCTION(BlueprintCallable, Category="PCG Utils|Data Cache")
	static bool LoadCacheDataAsset(const UPCGDataCacheComponent* CacheComponent,
		EPCGUtilsDataCache TargetCache, UPCGDataAsset*& OutDataAsset);

	UFUNCTION(BlueprintCallable, Category="PCG Utils|Data Cache")
	static bool LoadActorCacheDataAsset(const AActor* Actor, EPCGUtilsDataCache TargetCache,
		UPCGDataAsset*& OutDataAsset);

	/** Loads the referenced PCGDataAsset and copies its underlying PCG data collection. */
	UFUNCTION(BlueprintCallable, Category="PCG Utils|Data Cache")
	static bool GetCacheDataCollection(const UPCGDataCacheComponent* CacheComponent,
		EPCGUtilsDataCache TargetCache, FPCGDataCollection& OutDataCollection);

	UFUNCTION(BlueprintCallable, Category="PCG Utils|Data Cache")
	static bool GetActorCacheDataCollection(const AActor* Actor, EPCGUtilsDataCache TargetCache,
		FPCGDataCollection& OutDataCollection);
};
