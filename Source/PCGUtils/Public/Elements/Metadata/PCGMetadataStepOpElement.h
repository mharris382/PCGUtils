// Copyright Max Harris

#pragma once

#include "Elements/Metadata/PCGUtilsMetadataOpElementBase.h"

#include "PCGMetadataStepOpElement.generated.h"

UENUM()
enum class EPCGMetadataStepOperation : uint8
{
	/** Hermite interpolation: Alpha = saturate((In - Min) / (Max - Min)), Result = Alpha * Alpha * (3 - 2 * Alpha). */
	SmoothStep UMETA(DisplayName = "Smooth Step", SearchHints = "smoothstep hermite ease"),
	/** Hard threshold: 1 where In is at or above Edge, 0 below it. */
	Step UMETA(DisplayName = "Step", SearchHints = "threshold edge")
};

/**
 * Attribute operations replicating the material/HLSL step family:
 *
 *     Smooth Step: Alpha = saturate((In - Min) / (Max - Min)), Result = Alpha * Alpha * (3 - 2 * Alpha)
 *     Step:        Result = (In >= Edge) ? 1 : 0
 *
 * Same interface and behaviour as the engine's Attribute Maths Op: attribute or property sources, dynamic pin
 * typing, broadcasting and inline pin default values. Like those nodes, the pins follow the selected operation -
 * Smooth Step takes In/Min/Max, Step takes In/Edge.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (Keywords = "smoothstep smooth step remap ease hermite interpolate threshold edge saturate"))
class PCGUTILS_API UPCGMetadataStepOpSettings : public UPCGMetadataSettingsBase
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
#endif
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;
	//~End UPCGSettings interface

	//~Begin UPCGMetadataSettingsBase interface
	virtual FPCGAttributePropertyInputSelector GetInputSource(uint32 Index) const override;

	virtual FName GetInputPinLabel(uint32 Index) const override;
	virtual uint32 GetOperandNum() const override;
	virtual uint16 GetOutputType(uint16 InputTypeId) const override;

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const override;
	//~End UPCGMetadataSettingsBase interface

	//~Begin IPCGSettingsDefaultValueProvider interface
	virtual EPCGMetadataTypes GetPinInitialDefaultValueType(FName PinLabel) const override { return EPCGMetadataTypes::Double; }
	//~End IPCGSettingsDefaultValueProvider interface

protected:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override { return Super::GetChangeTypeForProperty(InPropertyName) | EPCGChangeType::Cosmetic; }
#endif
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMetadataStepOperation Operation = EPCGMetadataStepOperation::SmoothStep;

	/** The value to remap or threshold. Also the pin whose data is forwarded to the output by default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource1;

	/** Smooth Step: lower edge of the transition, at or below which the result is 0. Step: the threshold edge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource2;

	/** Smooth Step only: upper edge of the transition, at or above which the result is 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (EditCondition = "Operation == EPCGMetadataStepOperation::SmoothStep", EditConditionHides, PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource3;

	/**
	 * Both operations yield a value in [0, 1], so an integer input would collapse the result to 0 or 1.
	 * When the inputs are Int32/Int64, this forces the output attribute to be Double instead.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bForceOpToDouble = true;
};

class FPCGMetadataStepOpElement : public FPCGUtilsMetadataElementBase
{
protected:
	virtual bool DoOperation(PCGMetadataOps::FOperationData& OperationData) const override;
};
