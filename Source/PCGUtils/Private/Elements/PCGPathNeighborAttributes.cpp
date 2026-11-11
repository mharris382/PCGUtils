// Copyright Max Harris

#include "Elements/PCGPathNeighborAttributes.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsSettingsCategories.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGPathNeighborAttributes)

#define LOCTEXT_NAMESPACE "PCGUtilsPathNeighborAttributes"

FPCGUtilsPathNeighborAttributeEntry::FPCGUtilsPathNeighborAttributeEntry()
{
	Source.Update(TEXT("@Last"));
}

namespace PCGUtilsPathNeighbor
{
	int32 ResolveIndex(int32 Index, int32 NumPoints, int32 Offset, bool bClosed, EPCGUtilsPathNeighborEndMode EndMode, bool& bOutHasNeighbor)
	{
		bOutHasNeighbor = true;
		if (NumPoints <= 0)
		{
			bOutHasNeighbor = false;
			return INDEX_NONE;
		}

		const int64 Target = static_cast<int64>(Index) + Offset;
		if (bClosed)
		{
			const int64 Wrapped = ((Target % NumPoints) + NumPoints) % NumPoints;
			return static_cast<int32>(Wrapped);
		}

		if (Target >= 0 && Target < NumPoints)
		{
			return static_cast<int32>(Target);
		}

		bOutHasNeighbor = false;
		switch (EndMode)
		{
		case EPCGUtilsPathNeighborEndMode::Clamp: return Target < 0 ? 0 : NumPoints - 1;
		case EPCGUtilsPathNeighborEndMode::OwnValue: return Index;
		default: return INDEX_NONE;
		}
	}

	static void Error(FPCGContext* Context, const FString& Message)
	{
		PCGLog::LogErrorOnGraph(FText::FromString(TEXT("Path Neighbor Attributes: ") + Message), Context);
	}

	static FString EntryLabel(const FPCGUtilsPathNeighborAttributeEntry& Entry, int32 EntryIndex)
	{
		return FString::Printf(TEXT("Attributes[%d] ('%s' -> '%s')"), EntryIndex, *Entry.Source.GetDisplayText().ToString(), *Entry.OutputAttributeName.ToString());
	}

	/** Each failed condition is reported on its own. */
	static bool ValidateSettings(FPCGContext* Context, const UPCGUtilsPathNeighborAttributesSettings& Settings)
	{
		bool bValid = true;
		if (Settings.Attributes.IsEmpty())
		{
			Error(Context, TEXT("Attributes is empty; add at least one entry to read."));
			bValid = false;
		}

		if (Settings.ClosedMode == EPCGUtilsPathClosedMode::FromAttribute)
		{
			if (Settings.ClosedAttributeName.IsNone())
			{
				Error(Context, TEXT("Closed Attribute Name is None while Closed Mode is From Attribute; expected a Data-domain bool attribute name (default IsClosed)."));
				bValid = false;
			}
			else if (!FPCGMetadataAttributeBase::IsValidName(Settings.ClosedAttributeName))
			{
				Error(Context, FString::Printf(TEXT("Closed Attribute Name='%s' is not a valid bare attribute name; write it without '@Data.'."), *Settings.ClosedAttributeName.ToString()));
				bValid = false;
			}
		}

		for (int32 Index = 0; Index < Settings.Attributes.Num(); ++Index)
		{
			const FPCGUtilsPathNeighborAttributeEntry& Entry = Settings.Attributes[Index];
			if (Entry.OutputAttributeName.IsNone())
			{
				Error(Context, FString::Printf(TEXT("Attributes[%d] has Output Attribute Name None; it must name the attribute to write."), Index));
				bValid = false;
			}
			if (Entry.bOutputHasNeighbor && Entry.HasNeighborAttributeName.IsNone())
			{
				Error(Context, FString::Printf(TEXT("Attributes[%d] has Output Has Neighbor on but Has Neighbor Attribute Name None; name the bool to write or turn the flag off."), Index));
				bValid = false;
			}
		}
		return bValid;
	}

	/** Reads the closure flag. Returns false (after logging) when the attribute exists with the wrong type. */
	static bool ReadClosed(FPCGContext* Context, const UPCGUtilsPathNeighborAttributesSettings& Settings, const UPCGData* Input, int32 InputIndex, bool& bOutClosed)
	{
		bOutClosed = Settings.ClosedMode == EPCGUtilsPathClosedMode::Closed;
		if (Settings.ClosedMode != EPCGUtilsPathClosedMode::FromAttribute)
		{
			return true;
		}

		const FPCGMetadataDomain* DataDomain = Input->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data);
		const FPCGMetadataAttributeBase* Attribute = DataDomain ? DataDomain->GetConstAttribute(Settings.ClosedAttributeName) : nullptr;
		if (!Attribute)
		{
			bOutClosed = false; // Missing means open, as in PCGEx.
			return true;
		}

		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<bool>::Id)
		{
			Error(Context, FString::Printf(TEXT("input %d, @Data.%s has type %s; expected Boolean (Closed Attribute Name). This input was skipped."),
				InputIndex, *Settings.ClosedAttributeName.ToString(),
				*StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Attribute->GetTypeId()).ToString()));
			return false;
		}

		const PCGMetadataEntryKey Key = DataDomain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey;
		bOutClosed = static_cast<const FPCGMetadataAttribute<bool>*>(Attribute)->GetValueFromItemKey(Key);
		return true;
	}

	template <typename T>
	static bool CreateAttribute(UPCGData* Output, const FPCGAttributePropertyOutputSelector& Selector, const T& Default, bool bAllowsInterpolation)
	{
		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			return true; // A point property ($Density...) already exists.
		}
		FPCGMetadataDomain* Domain = Output->MutableMetadata() ? Output->MutableMetadata()->GetMetadataDomainFromSelector(Selector) : nullptr;
		return Domain && Domain->FindOrCreateAttribute<T>(Selector.GetAttributeName(), Default, bAllowsInterpolation, /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/true) != nullptr;
	}

	template <typename T>
	static bool WriteValues(UPCGData* Output, const FPCGAttributePropertyOutputSelector& Selector, const TArray<T>& Values)
	{
		TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(Output, Selector);
		TUniquePtr<IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateKeys(Output, Selector);
		return Accessor.IsValid() && Keys.IsValid() && Keys->GetNum() == Values.Num()
			&& Accessor->SetRange<T>(Values, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}

	/** Applies one entry to Output in place. Returns false (after logging) when the input must be skipped. */
	static bool ApplyEntry(FPCGContext* Context, const UPCGUtilsPathNeighborAttributesSettings& Settings, UPCGBasePointData* Output,
		int32 InputIndex, int32 EntryIndex, bool bClosed)
	{
		const FPCGUtilsPathNeighborAttributeEntry& Entry = Settings.Attributes[EntryIndex];
		const FString Label = EntryLabel(Entry, EntryIndex);
		const int32 NumPoints = Output->GetNumPoints();

		const FPCGAttributePropertyInputSelector SourceSelector = Entry.Source.CopyAndFixLast(Output);
		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Output, SourceSelector, /*bQuiet=*/true);
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Output, SourceSelector);
		if (!Accessor.IsValid() || !Keys.IsValid())
		{
			Error(Context, FString::Printf(TEXT("input %d, %s: Source '%s' is not an attribute or property of this data. This input was skipped."),
				InputIndex, *Label, *SourceSelector.GetDisplayText().ToString()));
			return false;
		}
		if (Keys->GetNum() != NumPoints)
		{
			Error(Context, FString::Printf(TEXT("input %d, %s: Source has %d values but the path has %d points; expected one value per point (a point attribute or property, not @Data). This input was skipped."),
				InputIndex, *Label, Keys->GetNum(), NumPoints));
			return false;
		}

		FPCGAttributePropertyOutputSelector OutputSelector;
		OutputSelector.Update(Entry.OutputAttributeName.ToString());

		// Neighbor index per point, shared by the value and the flag.
		TArray<int32> SourceIndices;
		TArray<bool> HasNeighbor;
		SourceIndices.SetNumUninitialized(NumPoints);
		HasNeighbor.SetNumUninitialized(NumPoints);
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			bool bHas = true;
			SourceIndices[Index] = ResolveIndex(Index, NumPoints, Entry.Offset, bClosed, Settings.EndMode, bHas);
			HasNeighbor[Index] = bHas;
		}

		const int16 Type = Accessor->GetUnderlyingType();
		FString Failure;
		const bool bWritten = PCGMetadataAttribute::CallbackWithRightType(static_cast<uint16>(Type), [&]<typename T>(T) -> bool
		{
			TArray<T> SourceValues;
			if (!PCGAttributeAccessorHelpers::ExtractAllValues(Accessor.Get(), Keys.Get(), SourceValues, EPCGAttributeAccessorFlags::StrictType))
			{
				Failure = TEXT("could not read the Source values");
				return false;
			}

			// The output inherits the source attribute's default and interpolation; a property source gets T{} and the type's trait.
			T Default{};
			bool bAllowsInterpolation = PCG::Private::MetadataTraits<T>::CanInterpolate != 0;
			if (SourceSelector.GetSelection() == EPCGAttributePropertySelection::Attribute)
			{
				const FPCGMetadataDomain* Domain = Output->ConstMetadata()->GetConstMetadataDomainFromSelector(SourceSelector);
				const FPCGMetadataAttributeBase* SourceAttribute = Domain ? Domain->GetConstAttribute(SourceSelector.GetAttributeName()) : nullptr;
				if (SourceAttribute && SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
				{
					Default = static_cast<const FPCGMetadataAttribute<T>*>(SourceAttribute)->GetValue(PCGDefaultValueKey);
					bAllowsInterpolation = SourceAttribute->AllowsInterpolation();
				}
			}

			TArray<T> OutValues;
			OutValues.Reserve(NumPoints);
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				const int32 SourceIndex = SourceIndices[Index];
				OutValues.Add(SourceIndex == INDEX_NONE ? Default : SourceValues[SourceIndex]);
			}

			if (!CreateAttribute<T>(Output, OutputSelector, Default, bAllowsInterpolation))
			{
				Failure = TEXT("could not create the output attribute");
				return false;
			}
			if (!WriteValues<T>(Output, OutputSelector, OutValues))
			{
				Failure = FString::Printf(TEXT("could not write %d values as %s; the output must be a point attribute, or a point property of a compatible type"),
					NumPoints, *StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Type).ToString());
				return false;
			}
			return true;
		});

		if (!bWritten)
		{
			Error(Context, FString::Printf(TEXT("input %d, %s: %s. This input was skipped."), InputIndex, *Label,
				Failure.IsEmpty() ? TEXT("Source type is not supported") : *Failure));
			return false;
		}

		if (Entry.bOutputHasNeighbor)
		{
			FPCGAttributePropertyOutputSelector FlagSelector;
			FlagSelector.Update(Entry.HasNeighborAttributeName.ToString());
			if (!CreateAttribute<bool>(Output, FlagSelector, false, false) || !WriteValues<bool>(Output, FlagSelector, HasNeighbor))
			{
				Error(Context, FString::Printf(TEXT("input %d, %s: could not write the bool '%s' (Has Neighbor Attribute Name). This input was skipped."),
					InputIndex, *Label, *Entry.HasNeighborAttributeName.ToString()));
				return false;
			}
		}
		return true;
	}
}

#if WITH_EDITOR
FText UPCGUtilsPathNeighborAttributesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Path | Neighbor Attributes");
}

FText UPCGUtilsPathNeighborAttributesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Copies attribute values from neighboring points along each path onto every point: the next point (Offset +1), the previous one (-1), or any offset.\n"
		"Closed paths (@Data.IsClosed by default) wrap around; open-path ends follow End Mode.\n"
		"Example: BarrierGeneration at +1 into NextBarrierGeneration, then split wherever the two differ.");
}

FString UPCGUtilsPathNeighborAttributesSettings::GetAdditionalTitleInformation() const
{
	if (Attributes.Num() == 1)
	{
		return Attributes[0].OutputAttributeName.ToString();
	}
	return Attributes.IsEmpty() ? FString() : FString::Printf(TEXT("%d attributes"), Attributes.Num());
}

EPCGSettingsType UPCGUtilsPathNeighborAttributesSettings::GetType() const
{
	return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Paths);
}
#endif

TArray<FPCGPinProperties> UPCGUtilsPathNeighborAttributesSettings::InputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
	Pin.SetRequiredPin();
	return { Pin };
}

TArray<FPCGPinProperties> UPCGUtilsPathNeighborAttributesSettings::OutputPinProperties() const
{
	return { FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point) };
}

FPCGElementPtr UPCGUtilsPathNeighborAttributesSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsPathNeighborAttributesElement>();
}

bool FPCGUtilsPathNeighborAttributesElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsPathNeighborAttributesElement::ExecuteInternal);
	const UPCGUtilsPathNeighborAttributesSettings* Settings = Context->GetInputSettings<UPCGUtilsPathNeighborAttributesSettings>();
	check(Settings);

	if (!PCGUtilsPathNeighbor::ValidateSettings(Context, *Settings))
	{
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		const UPCGBasePointData* InPoints = Cast<UPCGBasePointData>(Input.Data);
		if (!InPoints)
		{
			PCGUtilsPathNeighbor::Error(Context, FString::Printf(TEXT("input %d is %s; expected point data (a path). This input was skipped."),
				InputIndex, Input.Data ? *Input.Data->GetClass()->GetName() : TEXT("null")));
			continue;
		}

		bool bClosed = false;
		if (!PCGUtilsPathNeighbor::ReadClosed(Context, *Settings, InPoints, InputIndex, bClosed))
		{
			continue;
		}

		// The output is the input with attributes added: duplicating keeps every attribute, tag, property and @Data.
		UPCGBasePointData* Output = Cast<UPCGBasePointData>(InPoints->DuplicateData(Context));
		if (!Output)
		{
			PCGUtilsPathNeighbor::Error(Context, FString::Printf(TEXT("input %d could not be duplicated. This input was skipped."), InputIndex));
			continue;
		}

		bool bOk = true;
		if (Output->GetNumPoints() > 0)
		{
			for (int32 EntryIndex = 0; bOk && EntryIndex < Settings->Attributes.Num(); ++EntryIndex)
			{
				bOk = PCGUtilsPathNeighbor::ApplyEntry(Context, *Settings, Output, InputIndex, EntryIndex, bClosed);
			}
		}
		if (!bOk)
		{
			continue;
		}

		FPCGTaggedData& Out = Context->OutputData.TaggedData.Add_GetRef(Input);
		Out.Pin = PCGPinConstants::DefaultOutputLabel;
		Out.Data = Output;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
