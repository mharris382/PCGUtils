// Copyright Max Harris
// Input shorthands adapted from PCGExtendedToolkit (PCGExInputShorthandsDetails),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Details/PCGUtilsInputShorthands.h"

#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGData.h"
#include "PCGSettings.h"
#include "UObject/PropertyTag.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "PCGUtilsInputShorthands"

void FPCGUtilsInputShorthandBase::InitAttribute(FName InDefaultAttribute)
{
	// The engine default is @Last, which names no attribute in particular; a data read needs a real name.
	Attribute.SetAttributeName(InDefaultAttribute);
}

namespace PCGUtilsInputShorthands
{
	namespace
	{
		FText TypeText(EPCGMetadataTypes Type)
		{
			return StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
		}

		/** Which attribute types can be read as T, and how to describe them when one does not fit. */
		template <typename T>
		bool IsReadableAs(EPCGMetadataTypes Source, FText& OutExpected)
		{
			using E = EPCGMetadataTypes;
			if constexpr (std::is_same_v<T, bool>)
			{
				OutExpected = LOCTEXT("ExpectBool", "a Boolean");
				return Source == E::Boolean;
			}
			else if constexpr (std::is_integral_v<T>)
			{
				// No silent truncation: a fractional value cannot drive an integer setting.
				OutExpected = LOCTEXT("ExpectInteger", "an integer (Integer32 or Integer64)");
				return Source == E::Integer32 || Source == E::Integer64;
			}
			else if constexpr (std::is_floating_point_v<T>)
			{
				OutExpected = LOCTEXT("ExpectNumber", "a number (Integer32, Integer64, Float or Double)");
				return Source == E::Integer32 || Source == E::Integer64 || Source == E::Float || Source == E::Double;
			}
			else if constexpr (std::is_same_v<T, FVector2D>)
			{
				OutExpected = LOCTEXT("ExpectVector2", "a Vector2");
				return Source == E::Vector2;
			}
			else if constexpr (std::is_same_v<T, FVector>)
			{
				OutExpected = LOCTEXT("ExpectVector", "a Vector");
				return Source == E::Vector;
			}
			else if constexpr (std::is_same_v<T, FVector4>)
			{
				OutExpected = LOCTEXT("ExpectVector4", "a Vector4");
				return Source == E::Vector4;
			}
			else if constexpr (std::is_same_v<T, FRotator>)
			{
				OutExpected = LOCTEXT("ExpectRotator", "a Rotator");
				return Source == E::Rotator;
			}
			else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FString>)
			{
				OutExpected = LOCTEXT("ExpectText", "a Name or String");
				return Source == E::Name || Source == E::String;
			}
			else
			{
				static_assert(sizeof(T) == 0, "Unsupported shorthand value type");
				return false;
			}
		}

		template <typename T>
		bool ReadImpl(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, T& OutValue, FText& OutError)
		{
			const FText SelectorText = FText::FromString(InSelector.ToString());
			if (!InData)
			{
				OutError = FText::Format(LOCTEXT("NoData", "there is no input data to read '{0}' from"), SelectorText);
				return false;
			}

			const FPCGAttributePropertyInputSelector Selector = InSelector.CopyAndFixLast(InData);
			if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
			{
				OutError = FText::Format(LOCTEXT("NotAnAttribute",
					"'{0}' selects a property, not an attribute; only @Data attributes can be read here"), SelectorText);
				return false;
			}

			const FName AttributeName = Selector.GetAttributeName();
			if (AttributeName.IsNone())
			{
				OutError = LOCTEXT("NoAttributeName", "no attribute name is set; name the @Data attribute to read, or switch the setting to Constant");
				return false;
			}

			FPCGMetadataDomainID Domain = InData->GetMetadataDomainIDFromSelector(Selector);
			if (Domain == PCGMetadataDomainID::Default)
			{
				Domain = InData->GetDefaultMetadataDomainID();
			}
			if (Domain != PCGMetadataDomainID::Data)
			{
				OutError = FText::Format(LOCTEXT("NotDataDomain",
					"'{0}' does not select the @Data domain; this setting reads one value per input, so write it as '@Data.{1}'"),
					SelectorText, FText::FromName(AttributeName));
				return false;
			}

			const UPCGMetadata* Metadata = InData->ConstMetadata();
			const FPCGMetadataDomain* DataDomain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
			const FPCGMetadataAttributeBase* Attribute = DataDomain ? DataDomain->GetConstAttribute(AttributeName) : nullptr;
			if (!Attribute)
			{
				OutError = FText::Format(LOCTEXT("MissingAttribute",
					"attribute '{0}' was not found in the input's @Data domain"), FText::FromName(AttributeName));
				return false;
			}

			// Read through a selector pinned to @Data so extractors (.X, .Length) still apply.
			FPCGAttributePropertyInputSelector DataSelector = Selector;
			DataSelector.SetDomainName(PCGDataConstants::DataDomainName, /*bResetExtraNames=*/false);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, DataSelector, /*bQuiet=*/true);
			if (!Accessor)
			{
				OutError = FText::Format(LOCTEXT("BadExtractor",
					"'{0}' cannot be read: its extractor does not apply to @Data attribute '{1}', which is {2}"),
					SelectorText, FText::FromName(AttributeName), TypeText(static_cast<EPCGMetadataTypes>(Attribute->GetTypeId())));
				return false;
			}

			const EPCGMetadataTypes SourceType = static_cast<EPCGMetadataTypes>(Accessor->GetUnderlyingType());
			FText Expected;
			if (!IsReadableAs<T>(SourceType, Expected))
			{
				OutError = FText::Format(LOCTEXT("WrongType",
					"'{0}' is {1}; this setting needs {2}"), SelectorText, TypeText(SourceType), Expected);
				return false;
			}

			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(InData, DataSelector);
			if (!Keys || Keys->GetNum() < 1)
			{
				OutError = FText::Format(LOCTEXT("NoValue",
					"@Data attribute '{0}' exists but holds no value"), FText::FromName(AttributeName));
				return false;
			}

			if (!Accessor->Get<T>(OutValue, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				OutError = FText::Format(LOCTEXT("ReadFailed",
					"'{0}' ({1}) could not be read"), SelectorText, TypeText(SourceType));
				return false;
			}
			return true;
		}

		template <typename T>
		bool LoadMismatchedConstant(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, T& OutConstant)
		{
			const FName TagType = Tag.GetType().GetName();
			if constexpr (std::is_arithmetic_v<T>)
			{
				// A bool tag carries its value in the tag itself; the slot holds no payload.
				if (TagType == NAME_BoolProperty) { OutConstant = static_cast<T>(Tag.BoolVal != 0); return true; }
				if (TagType == NAME_IntProperty) { int32 Value = 0; Slot << Value; OutConstant = static_cast<T>(Value); return true; }
				if (TagType == NAME_Int64Property) { int64 Value = 0; Slot << Value; OutConstant = static_cast<T>(Value); return true; }
				if (TagType == NAME_FloatProperty) { float Value = 0.0f; Slot << Value; OutConstant = static_cast<T>(Value); return true; }
				if (TagType == NAME_DoubleProperty) { double Value = 0.0; Slot << Value; OutConstant = static_cast<T>(Value); return true; }
				return false;
			}
			else if constexpr (std::is_same_v<T, FName>)
			{
				if (TagType == NAME_NameProperty) { Slot << OutConstant; return true; }
				return false;
			}
			else if constexpr (std::is_same_v<T, FString>)
			{
				if (TagType == NAME_StrProperty) { Slot << OutConstant; return true; }
				return false;
			}
			else
			{
				return false;
			}
		}

		/** Range checks for the ranged shorthands; values are rejected, never clamped. */
		template <typename T>
		bool CheckRange(const T& Value, bool bHasMin, double Min, bool bHasMax, double Max, FText& OutError, const FPCGAttributePropertyInputSelector& Selector)
		{
			if constexpr (std::is_arithmetic_v<T>)
			{
				const double AsDouble = static_cast<double>(Value);
				if ((bHasMin && AsDouble < Min) || (bHasMax && AsDouble > Max))
				{
					const FText Range = bHasMax
						? FText::Format(LOCTEXT("RangeBoth", "from {0} to {1}"), FText::AsNumber(Min), FText::AsNumber(Max))
						: FText::Format(LOCTEXT("RangeMin", "at least {0}"), FText::AsNumber(Min));
					OutError = FText::Format(LOCTEXT("OutOfRange", "'{0}' is {1}; this setting must be {2}"),
						FText::FromString(Selector.ToString()), FText::AsNumber(AsDouble), Range);
					return false;
				}
			}
			return true;
		}
	}

	bool IsShorthandStruct(const UScriptStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FPCGUtilsInputShorthandBase::StaticStruct());
	}

	void ApplyOverrideLabels(TArray<FPCGSettingsOverridableParam>& InOutParams)
	{
		static const FName ConstantName = TEXT("Constant");
		static const FName AttributeName = TEXT("Attribute");

		// Desired label per shorthand param; NAME_None for every other param, which keeps its own label.
		TArray<FName> Desired;
		Desired.SetNum(InOutParams.Num());
		TMap<FName, int32> Claims;
		for (int32 Index = 0; Index < InOutParams.Num(); ++Index)
		{
			const FPCGSettingsOverridableParam& Param = InOutParams[Index];
			const int32 NumNames = Param.PropertiesNames.Num();
			const FStructProperty* Owner = Param.Properties.Num() >= 2
				? CastField<FStructProperty>(Param.Properties[Param.Properties.Num() - 2]) : nullptr;
			if (NumNames >= 2 && Owner && IsShorthandStruct(Owner->Struct))
			{
				const FString Member = Param.PropertiesNames[NumNames - 2].ToString();
				const FName Leaf = Param.PropertiesNames.Last();
				Desired[Index] = Leaf == ConstantName ? FName(*Member) : FName(*(Member + Leaf.ToString()));
			}
			Claims.FindOrAdd(Desired[Index].IsNone() ? Param.Label : Desired[Index])++;
		}

		for (int32 Index = 0; Index < InOutParams.Num(); ++Index)
		{
			if (Desired[Index].IsNone())
			{
				continue;
			}
			FPCGSettingsOverridableParam& Param = InOutParams[Index];
			if (Claims.FindChecked(Desired[Index]) == 1)
			{
				Param.Label = Desired[Index];
				Param.bHasNameClash = false;
			}
			else
			{
				// Same form as the engine's clash label (FPCGSettingsOverridableParam::GetPropertyPath, not exported).
				Param.Label = FName(*FString::JoinBy(Param.PropertiesNames, TEXT("/"), [](FName Name) { return Name.ToString(); }));
				Param.bHasNameClash = true;
			}
		}
	}

	FText FormatError(const FText& SettingLabel, int32 InputIndex, const FText& Message)
	{
		if (SettingLabel.IsEmpty())
		{
			return Message;
		}
		if (InputIndex == INDEX_NONE)
		{
			return FText::Format(LOCTEXT("ErrorNoIndex", "{0}: {1}"), SettingLabel, Message);
		}
		return FText::Format(LOCTEXT("ErrorWithIndex", "{0} (input {1}): {2}"), SettingLabel, FText::AsNumber(InputIndex), Message);
	}

#define PCGUTILS_READ_OVERLOAD(_TYPE) \
	bool ReadDataDomainValue(const UPCGData* InData, const FPCGAttributePropertyInputSelector& Selector, _TYPE& OutValue, FText& OutError) \
	{ return ReadImpl<_TYPE>(InData, Selector, OutValue, OutError); }

	PCGUTILS_READ_OVERLOAD(bool)
	PCGUTILS_READ_OVERLOAD(int32)
	PCGUTILS_READ_OVERLOAD(int64)
	PCGUTILS_READ_OVERLOAD(float)
	PCGUTILS_READ_OVERLOAD(double)
	PCGUTILS_READ_OVERLOAD(FVector2D)
	PCGUTILS_READ_OVERLOAD(FVector)
	PCGUTILS_READ_OVERLOAD(FVector4)
	PCGUTILS_READ_OVERLOAD(FRotator)
	PCGUTILS_READ_OVERLOAD(FName)
	PCGUTILS_READ_OVERLOAD(FString)

#undef PCGUTILS_READ_OVERLOAD
}

// _NAME, _TYPE, bHasMin, Min, bHasMax, Max
#define PCGUTILS_FOREACH_INPUT_SHORTHAND(MACRO) \
	MACRO(Boolean, bool, false, 0, false, 0) \
	MACRO(Integer32, int32, false, 0, false, 0) \
	MACRO(Integer32Abs, int32, true, 0, false, 0) \
	MACRO(Integer64, int64, false, 0, false, 0) \
	MACRO(Float, float, false, 0, false, 0) \
	MACRO(Double, double, false, 0, false, 0) \
	MACRO(DoubleAbs, double, true, 0, false, 0) \
	MACRO(Double01, double, true, 0, true, 1) \
	MACRO(Vector2, FVector2D, false, 0, false, 0) \
	MACRO(Vector, FVector, false, 0, false, 0) \
	MACRO(Vector4, FVector4, false, 0, false, 0) \
	MACRO(Rotator, FRotator, false, 0, false, 0) \
	MACRO(Name, FName, false, 0, false, 0) \
	MACRO(String, FString, false, 0, false, 0)

#define PCGUTILS_INPUT_SHORTHAND_IMPL(_NAME, _TYPE, _HASMIN, _MIN, _HASMAX, _MAX) \
	bool FPCGUtilsInputShorthand##_NAME::TryReadDataValue(const UPCGData* InData, _TYPE& OutValue, FText& OutError, \
		const FText& SettingLabel, int32 InputIndex) const \
	{ \
		if (IsConstant()) \
		{ \
			OutValue = Constant; \
			return true; \
		} \
		FText Reason; \
		_TYPE Value{}; \
		if (!PCGUtilsInputShorthands::ReadDataDomainValue(InData, Attribute, Value, Reason) || \
			!PCGUtilsInputShorthands::CheckRange<_TYPE>(Value, _HASMIN, _MIN, _HASMAX, _MAX, Reason, Attribute)) \
		{ \
			OutError = PCGUtilsInputShorthands::FormatError(SettingLabel, InputIndex, Reason); \
			return false; \
		} \
		OutValue = MoveTemp(Value); \
		return true; \
	} \
	bool FPCGUtilsInputShorthand##_NAME::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot) \
	{ \
		if (!PCGUtilsInputShorthands::LoadMismatchedConstant(Tag, Slot, Constant)) { return false; } \
		Input = EPCGUtilsInputValueType::Constant; \
		return true; \
	}

PCGUTILS_FOREACH_INPUT_SHORTHAND(PCGUTILS_INPUT_SHORTHAND_IMPL)

#undef PCGUTILS_INPUT_SHORTHAND_IMPL
#undef PCGUTILS_FOREACH_INPUT_SHORTHAND

#undef LOCTEXT_NAMESPACE
