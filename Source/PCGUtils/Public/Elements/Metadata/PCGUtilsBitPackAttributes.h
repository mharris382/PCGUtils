// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGUtilsBitPackAttributes.generated.h"

/** How an input value becomes the unsigned integer stored in its bit field. */
UENUM(BlueprintType)
enum class EPCGUtilsBitPackValueMode : uint8
{
	/** Rounds the value to the nearest whole number and clamps it to [0, 2^Bits - 1]. Use for ids, indices, counts and flags. */
	Integer,
	/** Clamps the value to [0, 1] and scales it to [0, 2^Bits - 1], rounding to the nearest step. Use for 0-1 weights, tints and masks. */
	Normalized
};

/** The type of the packed output attribute, which decides how many bits can be stored exactly. */
UENUM(BlueprintType)
enum class EPCGUtilsBitPackOutputType : uint8
{
	/** 32-bit float holding the packed number as a whole value. Exact up to 24 bits. Per Instance Custom Data is float, so this is the one to use for materials. */
	Float,
	/** 64-bit float holding the packed number as a whole value. Exact up to 53 bits. */
	Double,
	/** 32-bit integer holding the packed bits. Up to 32 bits; with all 32 used, a set top bit reads as a negative number. */
	Integer32 UMETA(DisplayName = "Integer 32"),
	/** 64-bit integer holding the packed bits. Up to 64 bits; with all 64 used, a set top bit reads as a negative number. */
	Integer64 UMETA(DisplayName = "Integer 64")
};

/** One field of the packed value. */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsBitPackEntry
{
	GENERATED_BODY()

	/**
	 * Required. Name of the attribute to pack, read from every input. Must be numeric (Float, Double, Integer 32,
	 * Integer 64) or Boolean. Accepts the usual selector syntax: '$Density' for a point property, '@Data.Name' for
	 * a data-domain attribute (one value, used for every element).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName AttributeName = NAME_None;

	/** Width of this field. Holds whole values 0 to 2^BitCount - 1 (4 bits: 0-15, 8 bits: 0-255). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "24"))
	int32 BitCount = 8;

	/** How the value becomes a whole number for the field. Values outside the field's range are clamped, with a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGUtilsBitPackValueMode ValueMode = EPCGUtilsBitPackValueMode::Integer;
};

namespace PCGUtilsBitPack
{
	/** Largest total bit count the output type stores exactly. */
	PCGUTILS_API int32 MaxBits(EPCGUtilsBitPackOutputType OutputType);

	/**
	 * Converts one value to its field's unsigned integer. bOutClamped is set when the value was outside the field's
	 * range (or NaN, which packs as 0).
	 */
	PCGUTILS_API uint64 Quantize(double Value, int32 BitCount, EPCGUtilsBitPackValueMode Mode, bool& bOutClamped);
}

/**
 * Packs several small numeric attributes into one number, so they can travel together in a single channel -
 * typically one float of Per Instance Custom Data for a material.
 *
 * The first entry takes the lowest bits, each next entry sits directly above the previous one. With entries of
 * 4, 4 and 8 bits, entry 0 is bits 0-3, entry 1 bits 4-7 and entry 2 bits 8-15. In a material, field k of a Float
 * output unpacks as: floor(Packed / 2^Offset) mod 2^Bits, where Offset is the sum of the bit counts before it.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils", meta = (Keywords = "bitpack bit pack bits pack attributes per instance custom data PICD material encode compress channel flags metadata attribute param point"))
class PCGUTILS_API UPCGUtilsBitPackAttributesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("BitPackAttributes")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual EPCGSettingsType GetType() const override;
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/**
	 * Required. Name of the attribute the packed value is written to; created if missing, replaced if it exists
	 * with another type. Its type is set by Output Type. Accepts '@Data.Name' to write one value for the whole data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	FName OutputAttributeName = TEXT("BitPacked");

	/** Type of the output attribute. Float is exact up to 24 bits in total, which is what Per Instance Custom Data can carry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	EPCGUtilsBitPackOutputType OutputType = EPCGUtilsBitPackOutputType::Float;

	/** The fields to pack, lowest bits first. Each names a required input attribute and how many bits it gets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (TitleProperty = "{AttributeName} ({BitCount} bits)"))
	TArray<FPCGUtilsBitPackEntry> AttributesToPack;

	/** Warn on the graph when values had to be clamped to fit their field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, AdvancedDisplay)
	bool bWarnOnClampedValues = true;
};

class PCGUTILS_API FPCGUtilsBitPackAttributesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
