#include "Elements/PCGUtilsCaptureRestorePointProperty.h"

#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGUtilsCaptureRestorePointProperty)

#define LOCTEXT_NAMESPACE "PCGUtilsCaptureRestorePointProperty"

namespace PCGUtilsCaptureRestoreNames
{
	const FName Density(TEXT("PCGUtils_CapturedDensity"));
	const FName PositionZ(TEXT("PCGUtils_CapturedPositionZ"));
	const FName BoundsMin(TEXT("PCGUtils_CapturedBoundsMin"));
	const FName BoundsMax(TEXT("PCGUtils_CapturedBoundsMax"));
}

namespace
{
	enum class EAlias : int32
	{
		CaptureDensity,
		RestoreDensity,
		CaptureZ,
		RestoreZ,
		CaptureBounds,
		RestoreBounds
	};

	FText PropertyText(EPCGUtilsCapturedPointProperty Property)
	{
		switch (Property)
		{
		case EPCGUtilsCapturedPointProperty::Density: return LOCTEXT("Density", "Density");
		case EPCGUtilsCapturedPointProperty::PositionZ: return LOCTEXT("Z", "Z");
		case EPCGUtilsCapturedPointProperty::Bounds: return LOCTEXT("Bounds", "Bounds");
		default: return FText::GetEmpty();
		}
	}

	template<typename T>
	bool HasExpectedAttributeType(const UPCGMetadata* Metadata, FName Name)
	{
		return !Metadata->GetConstAttribute(Name) || Metadata->GetConstTypedAttribute<T>(Name);
	}

	template<typename T>
	FPCGMetadataAttribute<T>* FindOrCreateCaptureAttribute(UPCGMetadata* Metadata, FName Name)
	{
		return Metadata->FindOrCreateAttribute<T>(Name, T{}, true, true, false);
	}

	void LogTypeConflict(FPCGContext* Context, FName Name, const TCHAR* ExpectedType)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("TypeConflict", "Capture/Restore Point Property: temporary attribute '{0}' has an incompatible type; expected {1}. The input was not modified."),
			FText::FromName(Name), FText::FromString(ExpectedType)), Context);
	}

	bool ValidateRestoreAttribute(FPCGContext* Context, const UPCGMetadata* Metadata, FName Name,
		const TCHAR* ExpectedType, const FText& Property)
	{
		if (!Metadata->GetConstAttribute(Name))
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingCapture", "Restore {0} could not find expected temporary attribute '{1}'. The input was not modified."),
				Property, FText::FromName(Name)), Context);
			return false;
		}
		return true;
	}
}

TArray<FPCGPinProperties> UPCGUtilsCaptureRestorePointPropertySettings::InputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point)};
}

TArray<FPCGPinProperties> UPCGUtilsCaptureRestorePointPropertySettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

#if WITH_EDITOR
FName UPCGUtilsCaptureRestorePointPropertySettings::GetDefaultNodeName() const
{
	return FName(*GetDefaultNodeTitle().ToString().Replace(TEXT(" "), TEXT("")));
}

FText UPCGUtilsCaptureRestorePointPropertySettings::GetDefaultNodeTitle() const
{
	return FText::Format(Operation == EPCGUtilsPointPropertyOperation::Capture
		? LOCTEXT("CaptureTitle", "Capture {0}") : LOCTEXT("RestoreTitle", "Restore {0}"), PropertyText(Property));
}

FText UPCGUtilsCaptureRestorePointPropertySettings::GetNodeTooltipText() const
{
	if (Operation == EPCGUtilsPointPropertyOperation::Capture)
	{
		switch (Property)
		{
		case EPCGUtilsCapturedPointProperty::Density: return LOCTEXT("CaptureDensityTip", "Stores each point's density in temporary metadata and can optionally replace the density with a new value.");
		case EPCGUtilsCapturedPointProperty::PositionZ: return LOCTEXT("CaptureZTip", "Stores each point's position Z in temporary metadata and can optionally replace it without changing X or Y.");
		case EPCGUtilsCapturedPointProperty::Bounds: return LOCTEXT("CaptureBoundsTip", "Stores each point's bounds and can optionally replace them with uniform extents.");
		}
	}
	else
	{
		switch (Property)
		{
		case EPCGUtilsCapturedPointProperty::Density: return LOCTEXT("RestoreDensityTip", "Restores point density previously stored by Capture Density and removes the temporary metadata.");
		case EPCGUtilsCapturedPointProperty::PositionZ: return LOCTEXT("RestoreZTip", "Restores position Z previously stored by Capture Z without changing X or Y.");
		case EPCGUtilsCapturedPointProperty::Bounds: return LOCTEXT("RestoreBoundsTip", "Restores bounds previously stored by Capture Bounds and removes the temporary metadata.");
		}
	}
	return FText::GetEmpty();
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsCaptureRestorePointPropertySettings::GetPreconfiguredInfo() const
{
	return {
		{static_cast<int32>(EAlias::CaptureDensity), LOCTEXT("CaptureDensity", "Capture Density"), LOCTEXT("CaptureDensityAliasTip", "Stores each point's density in temporary metadata and can optionally replace the density with a new value.")},
		{static_cast<int32>(EAlias::RestoreDensity), LOCTEXT("RestoreDensity", "Restore Density"), LOCTEXT("RestoreDensityAliasTip", "Restores point density previously stored by Capture Density and removes the temporary metadata.")},
		{static_cast<int32>(EAlias::CaptureZ), LOCTEXT("CaptureZ", "Capture Z"), LOCTEXT("CaptureZAliasTip", "Stores each point's position Z in temporary metadata and can optionally replace it without changing X or Y.")},
		{static_cast<int32>(EAlias::RestoreZ), LOCTEXT("RestoreZ", "Restore Z"), LOCTEXT("RestoreZAliasTip", "Restores position Z previously stored by Capture Z without changing X or Y.")},
		{static_cast<int32>(EAlias::CaptureBounds), LOCTEXT("CaptureBounds", "Capture Bounds"), LOCTEXT("CaptureBoundsAliasTip", "Stores each point's bounds and can optionally replace them with uniform extents.")},
		{static_cast<int32>(EAlias::RestoreBounds), LOCTEXT("RestoreBounds", "Restore Bounds"), LOCTEXT("RestoreBoundsAliasTip", "Restores bounds previously stored by Capture Bounds and removes the temporary metadata.")}
	};
}

void UPCGUtilsCaptureRestorePointPropertySettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& Info)
{
	switch (static_cast<EAlias>(Info.PreconfiguredIndex))
	{
	case EAlias::CaptureDensity: Operation = EPCGUtilsPointPropertyOperation::Capture; Property = EPCGUtilsCapturedPointProperty::Density; break;
	case EAlias::RestoreDensity: Operation = EPCGUtilsPointPropertyOperation::Restore; Property = EPCGUtilsCapturedPointProperty::Density; break;
	case EAlias::CaptureZ: Operation = EPCGUtilsPointPropertyOperation::Capture; Property = EPCGUtilsCapturedPointProperty::PositionZ; break;
	case EAlias::RestoreZ: Operation = EPCGUtilsPointPropertyOperation::Restore; Property = EPCGUtilsCapturedPointProperty::PositionZ; break;
	case EAlias::CaptureBounds: Operation = EPCGUtilsPointPropertyOperation::Capture; Property = EPCGUtilsCapturedPointProperty::Bounds; break;
	case EAlias::RestoreBounds: Operation = EPCGUtilsPointPropertyOperation::Restore; Property = EPCGUtilsCapturedPointProperty::Bounds; break;
	default: ensureMsgf(false, TEXT("Unknown Capture/Restore Point Property preconfiguration index: %d"), Info.PreconfiguredIndex); break;
	}
}
#endif

FPCGElementPtr UPCGUtilsCaptureRestorePointPropertySettings::CreateElement() const
{
	return MakeShared<FPCGUtilsCaptureRestorePointPropertyElement>();
}

bool FPCGUtilsCaptureRestorePointPropertyElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsCaptureRestorePointPropertyElement::ExecuteInternal);
	const UPCGUtilsCaptureRestorePointPropertySettings* Settings = Context->GetInputSettings<UPCGUtilsCaptureRestorePointPropertySettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if (Context->ShouldStop()) return false;
		const UPCGBasePointData* InputPoints = Cast<const UPCGBasePointData>(Input.Data);
		if (!InputPoints)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "Capture/Restore Point Property only accepts point data."), Context);
			continue;
		}

		UPCGBasePointData* OutputPoints = CastChecked<UPCGBasePointData>(InputPoints->DuplicateData(Context));
		// Make the output fully independent and materialize inherited metadata so successful restore can
		// permanently remove the temporary attributes without exposing them again through a parent.
		OutputPoints->Flatten();
		UPCGMetadata* Metadata = OutputPoints->MutableMetadata();
		const int32 NumPoints = OutputPoints->GetNumPoints();
		auto Entries = OutputPoints->GetConstMetadataEntryValueRange();
		bool bSuccess = true;
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Data = OutputPoints;

		if (Settings->GetOperation() == EPCGUtilsPointPropertyOperation::Capture)
		{
			if (Settings->GetProperty() == EPCGUtilsCapturedPointProperty::Density)
			{
				if (!HasExpectedAttributeType<float>(Metadata, PCGUtilsCaptureRestoreNames::Density)) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::Density, TEXT("float")); continue; }
				auto* Attribute = FindOrCreateCaptureAttribute<float>(Metadata, PCGUtilsCaptureRestoreNames::Density);
				auto Values = OutputPoints->GetDensityValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index) Attribute->SetValue(Entries[Index], Values[Index]);
				if (Settings->bSetValue) Values.Set(Settings->SetValue);
			}
			else if (Settings->GetProperty() == EPCGUtilsCapturedPointProperty::PositionZ)
			{
				if (!HasExpectedAttributeType<float>(Metadata, PCGUtilsCaptureRestoreNames::PositionZ)) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::PositionZ, TEXT("float")); continue; }
				auto* Attribute = FindOrCreateCaptureAttribute<float>(Metadata, PCGUtilsCaptureRestoreNames::PositionZ);
				auto Values = OutputPoints->GetTransformValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					FTransform Transform = Values[Index];
					Attribute->SetValue(Entries[Index], Transform.GetLocation().Z);
					if (Settings->bSetValue) { FVector Location = Transform.GetLocation(); Location.Z = Settings->SetValue; Transform.SetLocation(Location); Values[Index] = Transform; }
				}
			}
			else
			{
				if (!HasExpectedAttributeType<FVector>(Metadata, PCGUtilsCaptureRestoreNames::BoundsMin)) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::BoundsMin, TEXT("vector")); continue; }
				if (!HasExpectedAttributeType<FVector>(Metadata, PCGUtilsCaptureRestoreNames::BoundsMax)) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::BoundsMax, TEXT("vector")); continue; }
				auto* MinAttribute = FindOrCreateCaptureAttribute<FVector>(Metadata, PCGUtilsCaptureRestoreNames::BoundsMin);
				auto* MaxAttribute = FindOrCreateCaptureAttribute<FVector>(Metadata, PCGUtilsCaptureRestoreNames::BoundsMax);
				auto MinValues = OutputPoints->GetBoundsMinValueRange();
				auto MaxValues = OutputPoints->GetBoundsMaxValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index) { MinAttribute->SetValue(Entries[Index], MinValues[Index]); MaxAttribute->SetValue(Entries[Index], MaxValues[Index]); }
				if (Settings->bSetValue) { const FVector Extents(Settings->SetValue); MinValues.Set(-Extents); MaxValues.Set(Extents); }
			}
		}
		else if (Settings->GetProperty() == EPCGUtilsCapturedPointProperty::Density)
		{
			const auto* Attribute = Metadata->GetConstTypedAttribute<float>(PCGUtilsCaptureRestoreNames::Density);
			if (!ValidateRestoreAttribute(Context, Metadata, PCGUtilsCaptureRestoreNames::Density, TEXT("float"), PropertyText(Settings->GetProperty()))) bSuccess = false;
			else if (!Attribute) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::Density, TEXT("float")); bSuccess = false; }
			if (bSuccess) { auto Values = OutputPoints->GetDensityValueRange(); for (int32 Index = 0; Index < NumPoints; ++Index) Values[Index] = Attribute->GetValueFromItemKey(Entries[Index]); Metadata->DeleteAttribute(PCGUtilsCaptureRestoreNames::Density); }
		}
		else if (Settings->GetProperty() == EPCGUtilsCapturedPointProperty::PositionZ)
		{
			const auto* Attribute = Metadata->GetConstTypedAttribute<float>(PCGUtilsCaptureRestoreNames::PositionZ);
			if (!ValidateRestoreAttribute(Context, Metadata, PCGUtilsCaptureRestoreNames::PositionZ, TEXT("float"), PropertyText(Settings->GetProperty()))) bSuccess = false;
			else if (!Attribute) { LogTypeConflict(Context, PCGUtilsCaptureRestoreNames::PositionZ, TEXT("float")); bSuccess = false; }
			if (bSuccess) { auto Values = OutputPoints->GetTransformValueRange(); for (int32 Index = 0; Index < NumPoints; ++Index) { FTransform Transform = Values[Index]; FVector Location = Transform.GetLocation(); Location.Z = Attribute->GetValueFromItemKey(Entries[Index]); Transform.SetLocation(Location); Values[Index] = Transform; } Metadata->DeleteAttribute(PCGUtilsCaptureRestoreNames::PositionZ); }
		}
		else
		{
			const auto* MinAttribute = Metadata->GetConstTypedAttribute<FVector>(PCGUtilsCaptureRestoreNames::BoundsMin);
			const auto* MaxAttribute = Metadata->GetConstTypedAttribute<FVector>(PCGUtilsCaptureRestoreNames::BoundsMax);
			if (!ValidateRestoreAttribute(Context, Metadata, PCGUtilsCaptureRestoreNames::BoundsMin, TEXT("vector"), PropertyText(Settings->GetProperty())) ||
				!ValidateRestoreAttribute(Context, Metadata, PCGUtilsCaptureRestoreNames::BoundsMax, TEXT("vector"), PropertyText(Settings->GetProperty()))) bSuccess = false;
			else if (!MinAttribute || !MaxAttribute) { LogTypeConflict(Context, !MinAttribute ? PCGUtilsCaptureRestoreNames::BoundsMin : PCGUtilsCaptureRestoreNames::BoundsMax, TEXT("vector")); bSuccess = false; }
			if (bSuccess) { auto MinValues = OutputPoints->GetBoundsMinValueRange(); auto MaxValues = OutputPoints->GetBoundsMaxValueRange(); for (int32 Index = 0; Index < NumPoints; ++Index) { MinValues[Index] = MinAttribute->GetValueFromItemKey(Entries[Index]); MaxValues[Index] = MaxAttribute->GetValueFromItemKey(Entries[Index]); } Metadata->DeleteAttribute(PCGUtilsCaptureRestoreNames::BoundsMin); Metadata->DeleteAttribute(PCGUtilsCaptureRestoreNames::BoundsMax); }
		}

	}
	return true;
}

#undef LOCTEXT_NAMESPACE
