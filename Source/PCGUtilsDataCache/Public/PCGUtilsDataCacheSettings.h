#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PCGUtilsDataCacheSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Data Cache"))
class PCGUTILSDATACACHE_API UPCGUtilsDataCacheSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="Cache Folders", meta=(ContentDir, LongPackageName))
	FDirectoryPath RuntimeCacheFolder = FDirectoryPath(TEXT("/Game/PCGUtils/DataCache/Runtime"));

	UPROPERTY(Config, EditAnywhere, Category="Cache Folders", meta=(ContentDir, LongPackageName))
	FDirectoryPath OfflineCacheFolder = FDirectoryPath(TEXT("/Game/PCGUtils/DataCache/Offline"));

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("PCG Utils - Data Cache"); }
};
