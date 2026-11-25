#include "Elements/PCGUtilsCaptureRestorePointProperty.h"

#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGUtilsCaptureRestorePointProperty)

#define LOCTEXT_NAMESPACE "PCGUtilsCaptureRestorePointProperty"

namespace
{
	enum class EAlias : int32
	{
		CaptureDensity,
		RestoreDensity,
		CaptureZ,
		RestoreZ,
		CaptureBounds,
		RestoreBounds,
		CapturePosition,
		RestorePosition
	};

	FText PropertyText(EPCGUtilsCapturedPointProperty Property)
	{
		switch (Property)
		{
		case EPCGUtilsCapturedPointProperty::Density: return LOCTEXT("Density", "Density");
		case EPCGUtilsCapturedPointProperty::PositionZ: return LOCTEXT("Z", "Z");
		case EPCGUtilsCapturedPointProperty::Bounds: return LOCTEXT("Bounds", "Bounds");
		case EPCGUtilsCapturedPointProperty::Position: return LOCTEXT("Position", "Position");
		default: return FText::GetEmpty();
		}
	}

	void LogTypeConflict(FPCGContext* Context, FName Name, const TCHAR* ExpectedType)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("TypeConflict", "Capture/Restore Point Property: temporary attribute '{0}' has an incompatible type; expected {1}. The input was not modified."),
			FText::FromName(Name), FText::FromString(ExpectedType)), Context);
	}

	template<typename T>
	FPCGMetadataAttribute<T>* FindOrCreateCaptureAttribute(FPCGContext* Context, UPCGMetadata* Metadata,
		FName Name, const TCHAR* ExpectedType)
	{
		if (Name.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("InvalidCaptureName", "Capture attribute name '{0}' must be a valid, non-empty point attribute name."),
				FText::FromName(Name)), Context);
			return nullptr;
		}
		if (Metadata->GetConstAttribute(Name) && !Metadata->GetConstTypedAttribute<T>(Name))
		{
			LogTypeConflict(Context, Name, ExpectedType);
			return nullptr;
		}
		return Metadata->FindOrCreateAttribute<T>(Name, T{}, true, true, false);
	}

	template<typename T>
	const FPCGMetadataAttribute<T>* FindRestoreAttribute(FPCGContext* Context, const UPCGMetadata* Metadata,
		FName Name, const TCHAR* ExpectedType, const FText& Property)
	{
		const FPCGMetadataAttribute<T>* Attribute = Metadata->GetConstTypedAttribute<T>(Name);
		if (Attribute)
		{
			return Attribute;
		}
		if (Metadata->GetConstAttribute(Name))
		{
			LogTypeConflict(Context, Name, ExpectedType);
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingCapture", "Restore {0} could not find expected temporary attribute '{1}'. The input was not modified."),
				Property, FText::FromName(Name)), Context);
		}
		return nullptr;
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
		case EPCGUtilsCapturedPointProperty::Position: return LOCTEXT("CapturePositionTip", "Stores each point's XYZ location in temporary metadata.");
		}
	}
	else
	{
		switch (Property)
		{
		case EPCGUtilsCapturedPointProperty::Density: return LOCTEXT("RestoreDensityTip", "Restores point density previously stored by Capture Density and removes the temporary metadata.");
		case EPCGUtilsCapturedPointProperty::PositionZ: return LOCTEXT("RestoreZTip", "Restores position Z previously stored by Capture Z without changing X or Y.");
		case EPCGUtilsCapturedPointProperty::Bounds: return LOCTEXT("RestoreBoundsTip", "Restores bounds previously stored by Capture Bounds and removes the temporary metadata.");
		case EPCGUtilsCapturedPointProperty::Position: return LOCTEXT("RestorePositionTip", "Restores the XYZ location stored by Capture Position and removes the temporary attribute.");
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
		{static_cast<int32>(EAlias::RestoreBounds), LOCTEXT("RestoreBounds", "Restore Bounds"), LOCTEXT("RestoreBoundsAliasTip", "Restores bounds previously stored by Capture Bounds and removes the temporary metadata.")},
		{static_cast<int32>(EAlias::CapturePosition), LOCTEXT("CapturePosition", "Capture Position"), LOCTEXT("CapturePositionTip", "Stores each point's XYZ location in temporary metadata.")},
		{static_cast<int32>(EAlias::RestorePosition), LOCTEXT("RestorePosition", "Restore Position"), LOCTEXT("RestorePositionTip", "Restores the XYZ location stored by Capture Position and removes the temporary attribute.")}
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
	case EAlias::CapturePosition: Operation = EPCGUtilsPointPropertyOperation::Capture; Property = EPCGUtilsCapturedPointProperty::Position; break;
	case EAlias::RestorePosition: Operation = EPCGUtilsPointPropertyOperation::Restore; Property = EPCGUtilsCapturedPointProperty::Position; break;
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

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;

		const EPCGUtilsCapturedPointProperty Property = Settings->GetProperty();
		const FName BoundsMinName = Settings->BoundsMinAttributeName;
		const FName BoundsMaxName = Settings->BoundsMaxAttributeName;
		if (Property == EPCGUtilsCapturedPointProperty::Bounds && BoundsMinName == BoundsMaxName)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("IdenticalBoundsNames", "Bounds Min and Bounds Max must use distinct attribute names. Both are set to '{0}'."),
				FText::FromName(BoundsMinName)), Context);
			continue;
		}

		UPCGBasePointData* OutputPoints = CastChecked<UPCGBasePointData>(InputPoints->DuplicateData(Context));
		// Materialize inherited metadata so restore can remove temporary attributes from the output.
		OutputPoints->Flatten();
		UPCGMetadata* Metadata = OutputPoints->MutableMetadata();
		const int32 NumPoints = OutputPoints->GetNumPoints();

		if (Settings->GetOperation() == EPCGUtilsPointPropertyOperation::Capture)
		{
			// Points with no metadata still need distinct entries for their captured values.
			auto Entries = OutputPoints->GetMetadataEntryValueRange();
			for (int64& Entry : Entries)
			{
				Metadata->InitializeOnSet(Entry);
			}

			switch (Property)
			{
			case EPCGUtilsCapturedPointProperty::Density:
			{
				auto* Attribute = FindOrCreateCaptureAttribute<float>(Context, Metadata, Settings->DensityAttributeName, TEXT("float"));
				if (!Attribute) continue;
				auto Values = OutputPoints->GetDensityValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					Attribute->SetValue(Entries[Index], Values[Index]);
				}
				if (Settings->bSetValue) Values.Set(Settings->SetValue);
				break;
			}
			case EPCGUtilsCapturedPointProperty::PositionZ:
			{
				auto* Attribute = FindOrCreateCaptureAttribute<float>(Context, Metadata, Settings->ZAttributeName, TEXT("float"));
				if (!Attribute) continue;
				auto Values = OutputPoints->GetTransformValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					FTransform Transform = Values[Index];
					FVector Location = Transform.GetLocation();
					Attribute->SetValue(Entries[Index], Location.Z);
					if (Settings->bSetValue)
					{
						Location.Z = Settings->SetValue;
						Transform.SetLocation(Location);
						Values[Index] = Transform;
					}
				}
				break;
			}
			case EPCGUtilsCapturedPointProperty::Position:
			{
				auto* Attribute = FindOrCreateCaptureAttribute<FVector>(Context, Metadata, Settings->PositionAttributeName, TEXT("vector"));
				if (!Attribute) continue;
				const auto Values = OutputPoints->GetConstTransformValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					Attribute->SetValue(Entries[Index], Values[Index].GetLocation());
				}
				break;
			}
			case EPCGUtilsCapturedPointProperty::Bounds:
			{
				auto* MinAttribute = FindOrCreateCaptureAttribute<FVector>(Context, Metadata, BoundsMinName, TEXT("vector"));
				if (!MinAttribute) continue;
				auto* MaxAttribute = FindOrCreateCaptureAttribute<FVector>(Context, Metadata, BoundsMaxName, TEXT("vector"));
				if (!MaxAttribute) continue;
				auto MinValues = OutputPoints->GetBoundsMinValueRange();
				auto MaxValues = OutputPoints->GetBoundsMaxValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					MinAttribute->SetValue(Entries[Index], MinValues[Index]);
					MaxAttribute->SetValue(Entries[Index], MaxValues[Index]);
				}
				if (Settings->bSetValue)
				{
					const FVector Extents(Settings->SetValue);
					MinValues.Set(-Extents);
					MaxValues.Set(Extents);
				}
				break;
			}
			}
		}
		else
		{
			const auto Entries = OutputPoints->GetConstMetadataEntryValueRange();
			const FText DisplayProperty = PropertyText(Property);
			switch (Property)
			{
			case EPCGUtilsCapturedPointProperty::Density:
			{
				const FName Name = Settings->DensityAttributeName;
				const auto* Attribute = FindRestoreAttribute<float>(Context, Metadata, Name, TEXT("float"), DisplayProperty);
				if (!Attribute) continue;
				auto Values = OutputPoints->GetDensityValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					Values[Index] = Attribute->GetValueFromItemKey(Entries[Index]);
				}
				Metadata->DeleteAttribute(Name);
				break;
			}
			case EPCGUtilsCapturedPointProperty::PositionZ:
			{
				const FName Name = Settings->ZAttributeName;
				const auto* Attribute = FindRestoreAttribute<float>(Context, Metadata, Name, TEXT("float"), DisplayProperty);
				if (!Attribute) continue;
				auto Values = OutputPoints->GetTransformValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					FTransform Transform = Values[Index];
					FVector Location = Transform.GetLocation();
					Location.Z = Attribute->GetValueFromItemKey(Entries[Index]);
					Transform.SetLocation(Location);
					Values[Index] = Transform;
				}
				Metadata->DeleteAttribute(Name);
				break;
			}
			case EPCGUtilsCapturedPointProperty::Position:
			{
				const FName Name = Settings->PositionAttributeName;
				const auto* Attribute = FindRestoreAttribute<FVector>(Context, Metadata, Name, TEXT("vector"), DisplayProperty);
				if (!Attribute) continue;
				auto Values = OutputPoints->GetTransformValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					FTransform Transform = Values[Index];
					Transform.SetLocation(Attribute->GetValueFromItemKey(Entries[Index]));
					Values[Index] = Transform;
				}
				Metadata->DeleteAttribute(Name);
				break;
			}
			case EPCGUtilsCapturedPointProperty::Bounds:
			{
				const auto* MinAttribute = FindRestoreAttribute<FVector>(Context, Metadata, BoundsMinName, TEXT("vector"), DisplayProperty);
				if (!MinAttribute) continue;
				const auto* MaxAttribute = FindRestoreAttribute<FVector>(Context, Metadata, BoundsMaxName, TEXT("vector"), DisplayProperty);
				if (!MaxAttribute) continue;
				auto MinValues = OutputPoints->GetBoundsMinValueRange();
				auto MaxValues = OutputPoints->GetBoundsMaxValueRange();
				for (int32 Index = 0; Index < NumPoints; ++Index)
				{
					MinValues[Index] = MinAttribute->GetValueFromItemKey(Entries[Index]);
					MaxValues[Index] = MaxAttribute->GetValueFromItemKey(Entries[Index]);
				}
				Metadata->DeleteAttribute(BoundsMinName);
				Metadata->DeleteAttribute(BoundsMaxName);
				break;
			}
			}
		}

		Output.Data = OutputPoints;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
