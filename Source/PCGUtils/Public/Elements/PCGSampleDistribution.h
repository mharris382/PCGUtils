#pragma once

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "Curves/CurveFloat.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "Templates/UniquePtr.h"

#include "PCGSampleDistribution.generated.h"

UENUM(BlueprintType)
enum class EPCGSampleDistributionMode : uint8
{
	RandomBetweenRanges UMETA(
		DisplayName="Random Between Ranges",
		ToolTip="Interpolates between two min/max ranges using the Time attribute, then randomly samples inside the resulting range."),

	RandomBetweenCurves UMETA(
		DisplayName="Random Between Curves",
		ToolTip="Samples two curves using the Time attribute, then randomly interpolates between the sampled curve values.")
};

UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGSampleDistributionSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGSampleDistributionSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SampleDistribution")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual bool UseSeed() const override { return true; }
	virtual bool HasDynamicPins() const override { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_Overridable))
	EPCGSampleDistributionMode Mode = EPCGSampleDistributionMode::RandomBetweenRanges;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector TimeAttribute;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyOutputSelector OutputTarget;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Ranges",
		meta=(PCG_Overridable, EditCondition="Mode == EPCGSampleDistributionMode::RandomBetweenRanges", EditConditionHides))
	FVector2D StartRange = FVector2D(0.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Ranges",
		meta=(PCG_Overridable, EditCondition="Mode == EPCGSampleDistributionMode::RandomBetweenRanges", EditConditionHides))
	FVector2D EndRange = FVector2D(0.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Curves",
		meta=(PCG_Overridable, PCG_OverridableChildProperties="ExternalCurve", EditCondition="Mode == EPCGSampleDistributionMode::RandomBetweenCurves", EditConditionHides))
	FRuntimeFloatCurve MinCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Curves",
		meta=(PCG_Overridable, PCG_OverridableChildProperties="ExternalCurve", EditCondition="Mode == EPCGSampleDistributionMode::RandomBetweenCurves", EditConditionHides))
	FRuntimeFloatCurve MaxCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Time", meta=(PCG_Overridable))
	bool bClampTime = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Time", meta=(PCG_Overridable, EditCondition="bClampTime"))
	double TimeMin = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Time", meta=(PCG_Overridable, EditCondition="bClampTime"))
	double TimeMax = 1.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Random", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bHasCustomSeedSource = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Random", meta=(PCG_Overridable, EditCondition="bHasCustomSeedSource"))
	FPCGAttributePropertyInputSelector CustomSeedSource;
};

struct FPCGSampleDistributionContext : public FPCGContext
{
	int32 CurrentInput = 0;
	bool bDataPreparedForCurrentInput = false;

	FPCGAttributePropertyInputSelector TimeAttribute;
	FPCGAttributePropertyInputSelector CustomSeedSource;
	FPCGAttributePropertyOutputSelector OutputTarget;

	TUniquePtr<const IPCGAttributeAccessor> TimeAccessor;
	TUniquePtr<const IPCGAttributeAccessor> CustomSeedAccessor;
	TUniquePtr<IPCGAttributeAccessor> OutputAccessor;

	TUniquePtr<const IPCGAttributeAccessorKeys> TimeKeys;
	TUniquePtr<const IPCGAttributeAccessorKeys> CustomSeedKeys;
	TUniquePtr<IPCGAttributeAccessorKeys> OutputKeys;
};

class FPCGSampleDistributionElement : public IPCGElementWithCustomContext<FPCGSampleDistributionContext>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* Settings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};
