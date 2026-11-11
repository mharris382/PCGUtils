// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGUtilsPointShorthands.generated.h"

class UPCGBasePointData;

/** Where a point shorthand setting takes its value from. */
UENUM(BlueprintType)
enum class EPCGUtilsPointInputValueType : uint8
{
	Constant = 0 UMETA(DisplayName="Constant", ToolTip="Use the value set on the node, for every point.", ActionIcon="Constant"),
	Attribute = 1 UMETA(DisplayName="Attribute", ToolTip="Read the value from the input: a point attribute or property gives each point its own value; an @Data attribute (@Data.Name) gives one value for the whole input. The constant is never used as a fallback.", ActionIcon="DataAttribute")
};

/** The values a point shorthand read: one for every point (Constant, @Data) or one per point. */
template <typename T>
struct TPCGUtilsPointValues
{
	T Single{};
	TArray<T> PerPoint;

	bool IsPerPoint() const { return !PerPoint.IsEmpty(); }
	const T& Get(int32 Point) const { return PerPoint.IsEmpty() ? Single : PerPoint[Point]; }
};

/**
 * A setting that is a constant on the node, or an attribute read from each input: a point attribute or property
 * (one value per point) or an @Data attribute (one value per input). The per-point sibling of
 * FPCGUtilsInputShorthandBase, for nodes that work point by point.
 *
 * Attribute mode is strict: the attribute must exist on every input and hold a compatible type, and every value must
 * be in range (and finite, for numbers), or that input fails with an error naming the setting, the attribute, the
 * input index and, for a bad value, the point. The constant is not a fallback.
 *
 * Override pins work as for the input shorthands: <Member> for the constant, <Member>Attribute for the attribute.
 */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()

	/** Constant uses the value on the node; Attribute reads a point attribute or property, or an @Data attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable))
	EPCGUtilsPointInputValueType Input = EPCGUtilsPointInputValueType::Constant;

	/**
	 * The attribute read in Attribute mode: a point attribute ('Amount'), a property ('$Density') or an @Data
	 * attribute ('@Data.Amount', one value for the input). Extractors such as .X are allowed. Its required type is
	 * stated by the setting that owns this shorthand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	bool IsConstant() const { return Input == EPCGUtilsPointInputValueType::Constant; }
	bool IsAttribute() const { return Input == EPCGUtilsPointInputValueType::Attribute; }

	/** Switches to Attribute mode and reads Selector ('Name', '$Density', '@Data.Name'). */
	void SetAttribute(const FString& Selector)
	{
		Input = EPCGUtilsPointInputValueType::Attribute;
		Attribute.Update(Selector);
	}

protected:
	void InitAttribute(FName InDefaultAttribute) { Attribute.SetAttributeName(InDefaultAttribute); }
};

namespace PCGUtilsPointShorthands
{
	/** True for FPCGUtilsPointShorthandBase and every struct deriving from it. */
	PCGUTILSCORE_API bool IsPointShorthandStruct(const UScriptStruct* Struct);
}

/**
 * TryRead fills OutValues: the constant in Constant mode, one value for an @Data attribute, or one value per point.
 * On failure OutError holds the exact reason, prefixed with SettingLabel and InputIndex when given.
 */
#define PCGUTILS_POINT_SHORTHAND_BODY(_NAME, _TYPE) \
	FPCGUtilsPointShorthand##_NAME() { InitAttribute(NAME_None); } \
	explicit FPCGUtilsPointShorthand##_NAME(_TYPE InConstant, FName InDefaultAttribute = NAME_None) \
	{ \
		Constant = InConstant; \
		InitAttribute(InDefaultAttribute); \
	} \
	bool TryRead(const UPCGBasePointData* Points, TPCGUtilsPointValues<_TYPE>& OutValues, FText& OutError, \
		const FText& SettingLabel = FText::GetEmpty(), int32 InputIndex = INDEX_NONE) const; \
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandInteger32 : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(Integer32, int32)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	int32 Constant = 0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsPointShorthandInteger32)

/** Non-negative integer. An attribute value below 0 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandInteger32Abs : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(Integer32Abs, int32)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	int32 Constant = 0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsPointShorthandInteger32Abs)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandDouble : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(Double, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsPointShorthandDouble)

/** Non-negative number. An attribute value below 0 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandDoubleAbs : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(DoubleAbs, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsPointShorthandDoubleAbs)

/** Unit-range number. An attribute value outside 0..1 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandDouble01 : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(Double01, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsPointShorthandDouble01)

/** A vector. No range check; the owning setting validates the components it needs (whole numbers, at least 1, ...). */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsPointShorthandVector : public FPCGUtilsPointShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_POINT_SHORTHAND_BODY(Vector, FVector)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FVector Constant = FVector::ZeroVector;
};
