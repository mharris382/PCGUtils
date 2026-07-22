#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsDataCacheTypes.h"

class AActor;
class UPCGDataCacheComponent;

namespace PCGUtilsDataCacheHelpers
{
	bool NormalizeAndValidateFolder(const FString& InFolder, FString& OutFolder, FText& OutError);
	bool BuildAssetPath(EPCGUtilsDataCache TargetCache, FName CacheSaveName,
		FString& OutPackageName, FSoftObjectPath& OutObjectPath, FText& OutError);
	bool IsCacheAvailable(EPCGUtilsDataCache TargetCache);
	UPCGDataCacheComponent* FindSingleCacheComponent(const AActor* Actor, bool& bOutMultiple);
}
