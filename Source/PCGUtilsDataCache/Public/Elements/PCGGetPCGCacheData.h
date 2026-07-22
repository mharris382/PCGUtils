#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGUtilsDataCacheTypes.h"
#include "PCGGetPCGCacheData.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Data Cache",
	HideCategories=("Data Retrieval Settings"))
class PCGUTILSDATACACHE_API UPCGGetPCGCacheDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetPCGCacheDataSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache")
	EPCGUtilsDataCache TargetCache = EPCGUtilsDataCache::Runtime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache")
	EPCGUtilsDataCacheReferenceOutput OutputType = EPCGUtilsDataCacheReferenceOutput::PointData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data Cache", AdvancedDisplay)
	bool bSuppressSelfCacheWarning = false;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetPCGCacheData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
	virtual bool DisplayModeSettings() const override { return false; }
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::None; }

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDATACACHE_API FPCGGetPCGCacheDataElement : public FPCGDataFromActorElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings,
		const TArray<AActor*>& FoundActors) const override;
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
