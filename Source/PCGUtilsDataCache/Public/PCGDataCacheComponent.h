#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGUtilsDataCacheTypes.h"
#include "PCGDataCacheComponent.generated.h"

UCLASS(ClassGroup="PCG", meta=(BlueprintSpawnableComponent), BlueprintType)
class PCGUTILSDATACACHE_API UPCGDataCacheComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCGDataCacheComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache")
	bool bUsesRuntimeCache = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache")
	bool bUsesOfflineCache = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache|Asset")
	FText CacheDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache|Asset")
	FLinearColor CacheColor = FLinearColor(0.15f, 0.55f, 0.9f, 1.0f);

	UPROPERTY(VisibleAnywhere, Category="Data Cache", AdvancedDisplay)
	FName CacheSaveName;

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	bool GetCacheAssetPath(EPCGUtilsDataCache TargetCache, FSoftObjectPath& OutAssetPath) const;

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	bool GetRuntimeCacheAssetPath(FSoftObjectPath& OutAssetPath) const;

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	bool GetOfflineCacheAssetPath(FSoftObjectPath& OutAssetPath) const;

	UFUNCTION(BlueprintPure, Category="PCG Utils|Data Cache")
	bool UsesCache(EPCGUtilsDataCache TargetCache) const;

	virtual void OnRegister() override;
	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;

#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostEditImport() override;
#endif

private:
#if WITH_EDITOR
	bool SynchronizeCacheSaveName();
#endif
	void ValidateSingleComponent() const;
};
