// Copyright Max Harris

#pragma once

#include "Elements/Metadata/PCGUtilsMetadataOpElementBase.h"

#include "PCGMetadataSmoothStepElement.generated.h"

/**
 * Attribute operation replicating the material SmoothStep function:
 *
 *     Alpha = saturate((Value - Min) / (Max - Min))
 *     Result = Alpha * Alpha * (3 - 2 * Alpha)
 *
 * Same interface and behaviour as the engine's Attribute Maths Op ternary operations (Lerp / Clamp):
 * three input pins, attribute or property sources, broadcasting and inline pin default values.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (Keywords = "smoothstep smooth step remap ease hermite interpolate"))
class PCGUTILS_API UPCGMetadataSmoothStepSettings : public UPCGMetadataSettingsBase
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	//~End UPCGSettings interface

	//~Begin UPCGMetadataSettingsBase interface
	virtual FPCGAttributePropertyInputSelector GetInputSource(uint32 Index) const override;

	virtual FName GetInputPinLabel(uint32 Index) const override;
	virtual uint32 GetOperandNum() const override { return 3; }
	virtual uint16 GetOutputType(uint16 InputTypeId) const override;

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const override;
	//~End UPCGMetadataSettingsBase interface

	//~Begin IPCGSettingsDefaultValueProvider interface
	virtual EPCGMetadataTypes GetPinInitialDefaultValueType(FName PinLabel) const override { return EPCGMetadataTypes::Double; }
	//~End IPCGSettingsDefaultValueProvider interface

protected:
	//~Begin UPCGSettings interface
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/** The value to remap. Also the pin whose data is forwarded to the output by default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource1;

	/** Lower edge of the transition. Values at or below it produce 0. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource2;

	/** Upper edge of the transition. Values at or above it produce 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector InputSource3;

	/**
	 * SmoothStep always yields a value in [0, 1], so an integer input would collapse the result to 0 or 1.
	 * When the inputs are Int32/Int64, this forces the output attribute to be Double instead.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bForceOpToDouble = true;
};

class FPCGMetadataSmoothStepElement : public FPCGUtilsMetadataElementBase
{
protected:
	virtual bool DoOperation(PCGMetadataOps::FOperationData& OperationData) const override;
};
