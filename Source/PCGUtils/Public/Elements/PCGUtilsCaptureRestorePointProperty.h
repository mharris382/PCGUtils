#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGUtilsCaptureRestorePointProperty.generated.h"

UENUM()
enum class EPCGUtilsPointPropertyOperation : uint8
{
	Capture,
	Restore
};

UENUM()
enum class EPCGUtilsCapturedPointProperty : uint8
{
	Density,
	PositionZ,
	Bounds
};

/** Shared implementation for the six Capture/Restore native point-property aliases. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Point Attributes")
class PCGUTILS_API UPCGUtilsCaptureRestorePointPropertySettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set Value After Capture",
		meta=(EditCondition="Operation == EPCGUtilsPointPropertyOperation::Capture", EditConditionHides))
	bool bSetValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set Value After Capture",
		meta=(EditCondition="Operation == EPCGUtilsPointPropertyOperation::Capture && bSetValue", EditConditionHides, DisplayName="Value"))
	float SetValue = 0.0f;

	EPCGUtilsPointPropertyOperation GetOperation() const { return Operation; }
	EPCGUtilsCapturedPointProperty GetProperty() const { return Property; }

protected:
	virtual FPCGElementPtr CreateElement() const override;

private:
	// Serialized alias configuration. Intentionally not editable in the Details panel.
	UPROPERTY()
	EPCGUtilsPointPropertyOperation Operation = EPCGUtilsPointPropertyOperation::Capture;

	UPROPERTY()
	EPCGUtilsCapturedPointProperty Property = EPCGUtilsCapturedPointProperty::Density;
};

class PCGUTILS_API FPCGUtilsCaptureRestorePointPropertyElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
