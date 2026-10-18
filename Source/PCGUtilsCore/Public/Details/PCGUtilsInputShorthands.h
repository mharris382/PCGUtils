// Copyright Max Harris
// Input shorthands adapted from PCGExtendedToolkit (PCGExInputShorthandsDetails),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/StructOpsTypeTraits.h"

#include "PCGUtilsInputShorthands.generated.h"

class UPCGData;
struct FPropertyTag;
struct FPCGSettingsOverridableParam;

/** Where a shorthand setting takes its value from. */
UENUM(BlueprintType)
enum class EPCGUtilsInputValueType : uint8
{
	Constant = 0 UMETA(DisplayName="Constant", ToolTip="Use the value set on the node.", ActionIcon="Constant"),
	Attribute = 1 UMETA(DisplayName="@Data", ToolTip="Read one value per input from that input's @Data domain. The input must carry the attribute; the constant is never used as a fallback.", ActionIcon="DataAttribute")
};

/**
 * A setting that is either a constant on the node or a value read from each input's @Data domain.
 *
 * Attribute mode is strict: the attribute must exist on every input and hold a compatible type, or that input
 * fails with an error naming the setting, the attribute and the input index. The constant is not a fallback.
 *
 * Override pins: the Constant is overridden through a pin carrying the member's own name (Distance), so turning
 * a plain property into a shorthand keeps every existing override wire; the attribute name is overridden through
 * <Member>Attribute. See PCGUtilsInputShorthands::ApplyOverrideLabels.
 */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()

	/** Constant uses the value on the node; @Data reads it from each input's @Data domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable))
	EPCGUtilsInputValueType Input = EPCGUtilsInputValueType::Constant;

	/**
	 * The @Data attribute read in @Data mode. The @Data. prefix is optional on data whose default domain is @Data
	 * (DynMesh data); on point data write it explicitly. Extractors such as .X are allowed. Its required type is
	 * stated by the setting that owns this shorthand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	bool IsConstant() const { return Input == EPCGUtilsInputValueType::Constant; }
	bool IsAttribute() const { return Input == EPCGUtilsInputValueType::Attribute; }

protected:
	void InitAttribute(FName InDefaultAttribute);
};

namespace PCGUtilsInputShorthands
{
	/** True for FPCGUtilsInputShorthandBase and every struct deriving from it. */
	PCGUTILSCORE_API bool IsShorthandStruct(const UScriptStruct* Struct);

	/**
	 * Relabels the override params a settings class gathered for its shorthand members: <Member>/Constant becomes
	 * <Member>, <Member>/Attribute becomes <Member>Attribute. A label that would collide with another param keeps
	 * the full property path instead. Call from GatherOverridableParams() after Super.
	 */
	PCGUTILSCORE_API void ApplyOverrideLabels(TArray<FPCGSettingsOverridableParam>& InOutParams);

	/** Prefixes Message with the setting name and input index when they are known: "Distance (input 2): ...". */
	PCGUTILSCORE_API FText FormatError(const FText& SettingLabel, int32 InputIndex, const FText& Message);

	/**
	 * Reads one value from the @Data domain of InData through Selector. Fails, with the exact reason in OutError,
	 * when there is no data, the selector names a property or a non-@Data domain, the attribute is missing, its
	 * type cannot be read as the requested type, or it holds no value. Integer targets accept integer attributes
	 * only; floating-point targets accept any integer or floating-point attribute; Name and String interconvert.
	 */
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, bool& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, int32& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, int64& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, float& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, double& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FVector2D& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FVector& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FVector4& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FRotator& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FName& OutValue, FText& OutError);
	PCGUTILSCORE_API bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, FString& OutValue, FText& OutError);
}

/**
 * Members every typed shorthand shares.
 *
 * TryReadDataValue returns the constant in Constant mode. In @Data mode it reads InData's @Data domain and,
 * for a ranged type (Abs, 01), rejects a value outside the range rather than clamping it. SettingLabel and
 * InputIndex only shape the error text.
 */
#define PCGUTILS_INPUT_SHORTHAND_BODY(_NAME, _TYPE) \
	FPCGUtilsInputShorthand##_NAME() { InitAttribute(NAME_None); } \
	explicit FPCGUtilsInputShorthand##_NAME(_TYPE InConstant, FName InDefaultAttribute = NAME_None) \
	{ \
		Constant = InConstant; \
		InitAttribute(InDefaultAttribute); \
	} \
	bool TryReadDataValue(const UPCGData* InData, _TYPE& OutValue, FText& OutError, \
		const FText& SettingLabel = FText::GetEmpty(), int32 InputIndex = INDEX_NONE) const; \
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

/**
 * Lets a literal property become a shorthand of the same name with no rename and no PostLoad: a saved bare
 * scalar (bool, int, int64, float, double, Name, String) loads into Constant and Input stays Constant. Struct
 * values (vectors, rotators) have no such path; migrate those through a _DEPRECATED property and PostLoad.
 */
#define PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(_STRUCT) \
	template <> struct TStructOpsTypeTraits<_STRUCT> : public TStructOpsTypeTraitsBase2<_STRUCT> \
	{ enum { WithStructuredSerializeFromMismatchedTag = true }; };

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandBoolean : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Boolean, bool)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool Constant = false;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandBoolean)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandInteger32 : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Integer32, int32)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	int32 Constant = 0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandInteger32)

/** Non-negative integer. An @Data value below 0 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandInteger32Abs : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Integer32Abs, int32)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	int32 Constant = 0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandInteger32Abs)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandInteger64 : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Integer64, int64)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	int64 Constant = 0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandInteger64)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandFloat : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Float, float)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	float Constant = 0.0f;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandFloat)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandDouble : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Double, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandDouble)

/** Non-negative number. An @Data value below 0 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandDoubleAbs : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(DoubleAbs, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandDoubleAbs)

/** Unit-range number. An @Data value outside 0..1 is an error. */
USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandDouble01 : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Double01, double)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	double Constant = 0.0;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandDouble01)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandVector2 : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Vector2, FVector2D)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FVector2D Constant = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandVector : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Vector, FVector)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FVector Constant = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandVector4 : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Vector4, FVector4)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FVector4 Constant = FVector4(0, 0, 0, 1);
};

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandRotator : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Rotator, FRotator)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FRotator Constant = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandName : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(Name, FName)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FName Constant = NAME_None;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandName)

USTRUCT(BlueprintType)
struct PCGUTILSCORE_API FPCGUtilsInputShorthandString : public FPCGUtilsInputShorthandBase
{
	GENERATED_BODY()
	PCGUTILS_INPUT_SHORTHAND_BODY(String, FString)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FString Constant;
};
PCGUTILS_INPUT_SHORTHAND_MISMATCHED_TAG_TRAITS(FPCGUtilsInputShorthandString)
