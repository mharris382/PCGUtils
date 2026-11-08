// Copyright Max Harris

#include "Elements/Metadata/PCGUtilsBitPackAttributes.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsSettingsCategories.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGUtilsBitPackAttributes)

#define LOCTEXT_NAMESPACE "PCGUtilsBitPackAttributes"

namespace PCGUtilsBitPack
{
	int32 MaxBits(EPCGUtilsBitPackOutputType OutputType)
	{
		switch (OutputType)
		{
		case EPCGUtilsBitPackOutputType::Float: return 24;
		case EPCGUtilsBitPackOutputType::Double: return 53;
		case EPCGUtilsBitPackOutputType::Integer32: return 32;
		case EPCGUtilsBitPackOutputType::Integer64: return 64;
		default: return 0;
		}
	}

	static uint64 FieldMax(int32 BitCount)
	{
		return BitCount >= 64 ? MAX_uint64 : ((uint64(1) << BitCount) - 1);
	}

	uint64 Quantize(double Value, int32 BitCount, EPCGUtilsBitPackValueMode Mode, bool& bOutClamped)
	{
		bOutClamped = false;
		const uint64 Max = FieldMax(FMath::Clamp(BitCount, 1, 64));
		if (FMath::IsNaN(Value))
		{
			bOutClamped = true;
			return 0;
		}

		double Scaled = Value;
		if (Mode == EPCGUtilsBitPackValueMode::Normalized)
		{
			if (Value < 0.0 || Value > 1.0)
			{
				bOutClamped = true;
			}
			Scaled = FMath::Clamp(Value, 0.0, 1.0) * static_cast<double>(Max);
		}

		const double Rounded = FMath::RoundHalfFromZero(Scaled);
		if (Rounded <= 0.0)
		{
			bOutClamped |= Rounded < 0.0;
			return 0;
		}
		// double(Max) can round up past Max for wide fields, so compare in double and saturate.
		if (Rounded >= static_cast<double>(Max))
		{
			bOutClamped |= Rounded > static_cast<double>(Max);
			return Max;
		}
		return static_cast<uint64>(Rounded);
	}

	/** Integer inputs in Integer mode skip the double round trip, so 64-bit values pack exactly. */
	static uint64 QuantizeInteger(int64 Value, int32 BitCount, bool& bOutClamped)
	{
		const uint64 Max = FieldMax(FMath::Clamp(BitCount, 1, 64));
		bOutClamped = Value < 0 || static_cast<uint64>(Value) > Max;
		return Value < 0 ? 0 : FMath::Min(static_cast<uint64>(Value), Max);
	}

	static FText ModeText(EPCGUtilsBitPackValueMode Mode)
	{
		return StaticEnum<EPCGUtilsBitPackValueMode>()->GetDisplayNameTextByValue(static_cast<int64>(Mode));
	}

	static FText OutputTypeText(EPCGUtilsBitPackOutputType Type)
	{
		return StaticEnum<EPCGUtilsBitPackOutputType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
	}

	static FText MetadataTypeText(int16 Type)
	{
		return StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Type);
	}

	/** Reads one entry's values from an input and quantizes them. Returns false (after logging) when the entry cannot be read. */
	static bool ReadEntry(FPCGContext* Context, const UPCGData* Input, int32 InputIndex, int32 EntryIndex,
		const FPCGUtilsBitPackEntry& Entry, bool bWarnOnClamp, TArray<uint64>& OutFieldValues)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(Entry.AttributeName.ToString());
		Selector = Selector.CopyAndFixLast(Input);

		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Input, Selector, /*bQuiet=*/true);
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Input, Selector);
		if (!Accessor.IsValid() || !Keys.IsValid())
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingAttribute", "Bit Pack Attributes: input {0} has no attribute or property '{1}' (Attributes To Pack[{2}]). Every input must carry every packed attribute. This input was skipped."),
				InputIndex, FText::FromName(Entry.AttributeName), EntryIndex), Context);
			return false;
		}

		const int16 Type = Accessor->GetUnderlyingType();
		const bool bIsInteger = Type == static_cast<int16>(EPCGMetadataTypes::Integer32) || Type == static_cast<int16>(EPCGMetadataTypes::Integer64);
		const bool bIsReal = Type == static_cast<int16>(EPCGMetadataTypes::Float) || Type == static_cast<int16>(EPCGMetadataTypes::Double);
		const bool bIsBool = Type == static_cast<int16>(EPCGMetadataTypes::Boolean);
		if (!bIsInteger && !bIsReal && !bIsBool)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("UnsupportedType", "Bit Pack Attributes: input {0}, '{1}' (Attributes To Pack[{2}]) is of type {3}; expected Float, Double, Integer 32, Integer 64 or Boolean. This input was skipped."),
				InputIndex, FText::FromName(Entry.AttributeName), EntryIndex, MetadataTypeText(Type)), Context);
			return false;
		}

		const int32 Num = Keys->GetNum();
		OutFieldValues.SetNumUninitialized(Num);
		int32 NumClamped = 0;
		int32 FirstClampedIndex = INDEX_NONE;
		double FirstClampedValue = 0.0;
		auto Track = [&](bool bClamped, int32 Index, double Value)
		{
			if (bClamped)
			{
				if (NumClamped++ == 0)
				{
					FirstClampedIndex = Index;
					FirstClampedValue = Value;
				}
			}
		};

		bool bRead = true;
		if (bIsInteger && Entry.ValueMode == EPCGUtilsBitPackValueMode::Integer)
		{
			TArray<int64> Values;
			bRead = PCGAttributeAccessorHelpers::ExtractAllValues(Accessor.Get(), Keys.Get(), Values);
			for (int32 Index = 0; bRead && Index < Num; ++Index)
			{
				bool bClamped = false;
				OutFieldValues[Index] = QuantizeInteger(Values[Index], Entry.BitCount, bClamped);
				Track(bClamped, Index, static_cast<double>(Values[Index]));
			}
		}
		else
		{
			TArray<double> Values;
			if (bIsBool)
			{
				TArray<bool> Bools;
				bRead = PCGAttributeAccessorHelpers::ExtractAllValues(Accessor.Get(), Keys.Get(), Bools);
				Values.SetNumUninitialized(Bools.Num());
				for (int32 Index = 0; Index < Bools.Num(); ++Index) Values[Index] = Bools[Index] ? 1.0 : 0.0;
			}
			else
			{
				bRead = PCGAttributeAccessorHelpers::ExtractAllValues(Accessor.Get(), Keys.Get(), Values);
			}
			for (int32 Index = 0; bRead && Index < Num; ++Index)
			{
				bool bClamped = false;
				OutFieldValues[Index] = Quantize(Values[Index], Entry.BitCount, Entry.ValueMode, bClamped);
				Track(bClamped, Index, Values[Index]);
			}
		}

		if (!bRead)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("ReadFailed", "Bit Pack Attributes: input {0}, could not read the values of '{1}' (Attributes To Pack[{2}], type {3}). This input was skipped."),
				InputIndex, FText::FromName(Entry.AttributeName), EntryIndex, MetadataTypeText(Type)), Context);
			return false;
		}

		if (NumClamped > 0 && bWarnOnClamp)
		{
			const FText Range = Entry.ValueMode == EPCGUtilsBitPackValueMode::Normalized
				? LOCTEXT("NormalizedRange", "0 to 1")
				: FText::Format(LOCTEXT("IntegerRange", "0 to {0}"), FText::AsNumber(FieldMax(Entry.BitCount), &FNumberFormattingOptions::DefaultNoGrouping()));
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("Clamped", "Bit Pack Attributes: input {0}, '{1}' (Attributes To Pack[{2}], {3}, {4} bits) expects {5}; {6} of {7} values were out of range and were clamped. First at index {8}, value {9}."),
				{ FText::AsNumber(InputIndex), FText::FromName(Entry.AttributeName), FText::AsNumber(EntryIndex), ModeText(Entry.ValueMode),
				  FText::AsNumber(Entry.BitCount), Range, FText::AsNumber(NumClamped), FText::AsNumber(Num), FText::AsNumber(FirstClampedIndex),
				  FText::FromString(FString::SanitizeFloat(FirstClampedValue)) }), Context);
		}

		return true;
	}

	/** Validates the settings independently of any input; each failed condition is reported on its own. */
	static bool ValidateSettings(FPCGContext* Context, const UPCGUtilsBitPackAttributesSettings& Settings)
	{
		bool bValid = true;
		if (Settings.OutputAttributeName.IsNone())
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoOutputName", "Bit Pack Attributes: Output Attribute Name is None; it must name the attribute to write."), Context);
			bValid = false;
		}

		if (Settings.AttributesToPack.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoEntries", "Bit Pack Attributes: Attributes To Pack is empty; add at least one attribute to pack."), Context);
			bValid = false;
		}

		int64 TotalBits = 0;
		for (int32 Index = 0; Index < Settings.AttributesToPack.Num(); ++Index)
		{
			const FPCGUtilsBitPackEntry& Entry = Settings.AttributesToPack[Index];
			if (Entry.AttributeName.IsNone())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("EntryNoName", "Bit Pack Attributes: Attributes To Pack[{0}] has Attribute Name None; it must name an input attribute."), Index), Context);
				bValid = false;
			}
			if (Entry.BitCount < 1 || Entry.BitCount > 64)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("EntryBadBits", "Bit Pack Attributes: Attributes To Pack[{0}] ('{1}') has Bit Count {2}; expected 1 to 64."),
					Index, FText::FromName(Entry.AttributeName), Entry.BitCount), Context);
				bValid = false;
			}
			TotalBits += Entry.BitCount;
		}

		const int32 Limit = MaxBits(Settings.OutputType);
		if (TotalBits > Limit)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("TooManyBits", "Bit Pack Attributes: the bit counts in Attributes To Pack add up to {0}, but Output Type {1} stores at most {2} bits exactly. Reduce the bit counts or pick a wider Output Type."),
				FText::AsNumber(TotalBits), OutputTypeText(Settings.OutputType), Limit), Context);
			bValid = false;
		}
		return bValid;
	}

	template <typename T>
	static bool WritePacked(UPCGData* Output, const FPCGAttributePropertyOutputSelector& Selector, const TArray<uint64>& Packed)
	{
		TArray<T> Values;
		Values.SetNumUninitialized(Packed.Num());
		for (int32 Index = 0; Index < Packed.Num(); ++Index)
		{
			if constexpr (std::is_same_v<T, int32>)
			{
				Values[Index] = static_cast<int32>(static_cast<uint32>(Packed[Index]));
			}
			else
			{
				Values[Index] = static_cast<T>(Packed[Index]);
			}
		}

		TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(Output, Selector);
		TUniquePtr<IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateKeys(Output, Selector);
		return Accessor.IsValid() && Keys.IsValid() && Keys->GetNum() == Values.Num()
			&& Accessor->SetRange<T>(Values, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}

	template <typename T>
	static bool CreateOutputAttribute(UPCGData* Output, const FPCGAttributePropertyOutputSelector& Selector)
	{
		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			return true; // A point property ($Density...) already exists.
		}
		UPCGMetadata* Metadata = Output->MutableMetadata();
		FPCGMetadataDomain* Domain = Metadata ? Metadata->GetMetadataDomainFromSelector(Selector) : nullptr;
		// Packed numbers must never be blended between elements, so the attribute does not allow interpolation.
		return Domain && Domain->FindOrCreateAttribute<T>(Selector.GetAttributeName(), T{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/true) != nullptr;
	}
}

#if WITH_EDITOR
FText UPCGUtilsBitPackAttributesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Param | Bit Pack Attributes");
}

FText UPCGUtilsBitPackAttributesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Packs several small numeric attributes into one number, for example to send them to a material in one float of Per Instance Custom Data.\n"
		"The first entry takes the lowest bits. Unpack field k in a material with floor(Packed / 2^Offset) mod 2^Bits, where Offset is the sum of the bit counts before it.\n"
		"A Float output holds 24 bits exactly.");
}

FString UPCGUtilsBitPackAttributesSettings::GetAdditionalTitleInformation() const
{
	int32 TotalBits = 0;
	for (const FPCGUtilsBitPackEntry& Entry : AttributesToPack) TotalBits += Entry.BitCount;
	return FString::Printf(TEXT("%s, %d bits"), *OutputAttributeName.ToString(), TotalBits);
}

EPCGSettingsType UPCGUtilsBitPackAttributesSettings::GetType() const
{
	return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Params);
}
#endif

TArray<FPCGPinProperties> UPCGUtilsBitPackAttributesSettings::InputPinProperties() const
{
	return { FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::PointOrParam) };
}

TArray<FPCGPinProperties> UPCGUtilsBitPackAttributesSettings::OutputPinProperties() const
{
	return { FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::PointOrParam) };
}

FPCGElementPtr UPCGUtilsBitPackAttributesSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsBitPackAttributesElement>();
}

bool FPCGUtilsBitPackAttributesElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsBitPackAttributesElement::ExecuteInternal);
	const UPCGUtilsBitPackAttributesSettings* Settings = Context->GetInputSettings<UPCGUtilsBitPackAttributesSettings>();
	check(Settings);

	if (!PCGUtilsBitPack::ValidateSettings(Context, *Settings))
	{
		return true;
	}

	FPCGAttributePropertyOutputSelector OutputSelector;
	OutputSelector.Update(Settings->OutputAttributeName.ToString());

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		if (!Input.Data || !Input.Data->ConstMetadata())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoMetadata", "Bit Pack Attributes: input {0} carries no metadata; expected point data or an attribute set. This input was skipped."), InputIndex), Context);
			continue;
		}

		// The output is the input with one attribute added: duplicating keeps every attribute, tag and property.
		UPCGData* Output = Input.Data->DuplicateData(Context);
		const EPCGUtilsBitPackOutputType OutputType = Settings->OutputType;
		bool bCreated = false;
		switch (OutputType)
		{
		case EPCGUtilsBitPackOutputType::Float: bCreated = PCGUtilsBitPack::CreateOutputAttribute<float>(Output, OutputSelector); break;
		case EPCGUtilsBitPackOutputType::Double: bCreated = PCGUtilsBitPack::CreateOutputAttribute<double>(Output, OutputSelector); break;
		case EPCGUtilsBitPackOutputType::Integer32: bCreated = PCGUtilsBitPack::CreateOutputAttribute<int32>(Output, OutputSelector); break;
		case EPCGUtilsBitPackOutputType::Integer64: bCreated = PCGUtilsBitPack::CreateOutputAttribute<int64>(Output, OutputSelector); break;
		}

		TUniquePtr<IPCGAttributeAccessorKeys> OutputKeys = bCreated ? PCGAttributeAccessorHelpers::CreateKeys(Output, OutputSelector) : nullptr;
		if (!OutputKeys.IsValid())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadOutput", "Bit Pack Attributes: input {0} cannot hold the output '{1}' (Output Attribute Name); the name, domain or property does not exist on this data. This input was skipped."),
				InputIndex, FText::FromName(Settings->OutputAttributeName)), Context);
			continue;
		}

		const int32 NumOut = OutputKeys->GetNum();
		TArray<uint64> Packed;
		Packed.SetNumZeroed(NumOut);
		bool bInputOk = true;
		int32 BitOffset = 0;
		TArray<uint64> FieldValues;
		for (int32 EntryIndex = 0; bInputOk && EntryIndex < Settings->AttributesToPack.Num(); ++EntryIndex)
		{
			const FPCGUtilsBitPackEntry& Entry = Settings->AttributesToPack[EntryIndex];
			if (!PCGUtilsBitPack::ReadEntry(Context, Input.Data, InputIndex, EntryIndex, Entry, Settings->bWarnOnClampedValues, FieldValues))
			{
				bInputOk = false;
				break;
			}

			// One value (a data-domain attribute) is used for every element; otherwise the counts must match.
			if (FieldValues.Num() != NumOut && FieldValues.Num() != 1)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("CountMismatch", "Bit Pack Attributes: input {0}, '{1}' (Attributes To Pack[{2}]) has {3} values but the output '{4}' has {5}; expected {5} values or a single value. This input was skipped."),
					{ FText::AsNumber(InputIndex), FText::FromName(Entry.AttributeName), FText::AsNumber(EntryIndex), FText::AsNumber(FieldValues.Num()),
					  FText::FromName(Settings->OutputAttributeName), FText::AsNumber(NumOut) }), Context);
				bInputOk = false;
				break;
			}

			for (int32 Index = 0; Index < NumOut; ++Index)
			{
				const uint64 Field = FieldValues.Num() == 1 ? FieldValues[0] : FieldValues[Index];
				Packed[Index] |= Field << BitOffset; // BitOffset < 64: the total was validated against MaxBits.
			}
			BitOffset += Entry.BitCount;
		}

		if (!bInputOk)
		{
			continue;
		}

		bool bWritten = false;
		switch (OutputType)
		{
		case EPCGUtilsBitPackOutputType::Float: bWritten = PCGUtilsBitPack::WritePacked<float>(Output, OutputSelector, Packed); break;
		case EPCGUtilsBitPackOutputType::Double: bWritten = PCGUtilsBitPack::WritePacked<double>(Output, OutputSelector, Packed); break;
		case EPCGUtilsBitPackOutputType::Integer32: bWritten = PCGUtilsBitPack::WritePacked<int32>(Output, OutputSelector, Packed); break;
		case EPCGUtilsBitPackOutputType::Integer64: bWritten = PCGUtilsBitPack::WritePacked<int64>(Output, OutputSelector, Packed); break;
		}

		if (!bWritten)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("WriteFailed", "Bit Pack Attributes: input {0}, could not write {1} values to '{2}' as {3}. This input was skipped."),
				InputIndex, NumOut, FText::FromName(Settings->OutputAttributeName), PCGUtilsBitPack::OutputTypeText(OutputType)), Context);
			continue;
		}

		FPCGTaggedData& Out = Context->OutputData.TaggedData.Add_GetRef(Input);
		Out.Pin = PCGPinConstants::DefaultOutputLabel;
		Out.Data = Output;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
