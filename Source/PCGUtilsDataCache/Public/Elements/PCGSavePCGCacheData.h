#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGUtilsDataCacheTypes.h"
#include "PCGSavePCGCacheData.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILSDATACACHE_API UPCGSavePCGCacheDataSettings final : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGSavePCGCacheDataSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	EPCGUtilsDataCache SaveTargetCache = EPCGUtilsDataCache::Offline;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SavePCGCacheData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGSavePCGCacheDataElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};
