#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetActorBakeSettings.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGGetActorBakeSettingsSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetActorBakeSettingsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetActorBakeSettings")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Param; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bExtractFullSoftObjectPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bExtractSaveName = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bExtractSavePath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bExtractPreBakeGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bExtractPostBakeGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FString Label;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetActorBakeSettingsElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
