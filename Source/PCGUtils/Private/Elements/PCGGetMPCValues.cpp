// Copyright Max Harris

#include "Elements/PCGGetMPCValues.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetMPCValues"

#if WITH_EDITOR
FText UPCGGetMPCValuesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Param | Get MPC Values");
}

FText UPCGGetMPCValuesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Reads parameters of a Material Parameter Collection into an attribute set with one entry: one attribute per "
		"listed parameter. Source picks the asset's defaults or the values the world's instance holds now.");
}

void UPCGGetMPCValuesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// A newly picked parameter writes an attribute of the same name until the user names it otherwise.
	for (FPCGUtilsMPCParameterRead& Read : Parameters)
	{
		if (Read.AttributeName.IsNone() && !Read.Parameter.IsNone())
		{
			Read.AttributeName = Read.Parameter;
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGGetMPCValuesSettings::AddAllParameters()
{
	const UMaterialParameterCollection* Loaded = Collection.LoadSynchronous();
	if (!Loaded)
	{
		return;
	}
	TSet<FName> Listed;
	for (const FPCGUtilsMPCParameterRead& Read : Parameters)
	{
		Listed.Add(Read.Parameter);
	}

	Modify();
	for (const bool bVectors : {false, true})
	{
		TArray<FName> Names;
		Loaded->GetParameterNames(Names, bVectors);
		for (const FName Name : Names)
		{
			if (!Listed.Contains(Name))
			{
				FPCGUtilsMPCParameterRead& Read = Parameters.AddDefaulted_GetRef();
				Read.Parameter = Name;
				Read.AttributeName = Name;
				Listed.Add(Name);
			}
		}
	}
	FProperty* Property = FindFProperty<FProperty>(StaticClass(), GET_MEMBER_NAME_CHECKED(UPCGGetMPCValuesSettings, Parameters));
	FPropertyChangedEvent Event(Property, EPropertyChangeType::ArrayAdd);
	PostEditChangeProperty(Event);
}
#endif

FString UPCGGetMPCValuesSettings::GetAdditionalTitleInformation() const
{
	return Collection.IsNull() ? FString() : Collection.GetAssetName();
}

TArray<FString> UPCGGetMPCValuesSettings::GetMPCParameterNameOptions() const
{
	TArray<FString> Options;
	if (const UMaterialParameterCollection* Loaded = Collection.LoadSynchronous())
	{
		for (const bool bVectors : {false, true})
		{
			TArray<FName> Names;
			Loaded->GetParameterNames(Names, bVectors);
			for (const FName Name : Names)
			{
				Options.Add(Name.ToString());
			}
		}
	}
	return Options;
}

TArray<FPCGPinProperties> UPCGGetMPCValuesSettings::OutputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("OutTip", "One attribute set with a single entry: one attribute per listed parameter.");
#endif
	return {Pin};
}

FPCGElementPtr UPCGGetMPCValuesSettings::CreateElement() const
{
	return MakeShared<FPCGGetMPCValuesElement>();
}

namespace PCGGetMPCValuesHelpers
{
	FText EntryLabel(int32 Index)
	{
		return FText::Format(LOCTEXT("EntryLabel", "Parameters[{0}]"), FText::AsNumber(Index));
	}

	template <typename T>
	void WriteMPCValue(UPCGMetadata& Metadata, int64 EntryKey, FName AttributeName, const T& Value)
	{
		if (FPCGMetadataAttribute<T>* Attribute = Metadata.CreateAttribute<T>(AttributeName, Value, true, false))
		{
			Attribute->SetValue(EntryKey, Value);
		}
	}
}

UPCGParamData* FPCGGetMPCValuesElement::ReadValues(FPCGContext* Context, const UPCGGetMPCValuesSettings& Settings,
	const UMaterialParameterCollection* Collection, const UWorld* World)
{
	using namespace PCGGetMPCValuesHelpers;
	check(Collection);

	const UMaterialParameterCollectionInstance* Instance = nullptr;
	bool bValid = true;
	if (Settings.Source == EPCGUtilsMPCValueSource::World)
	{
		if (!World)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoWorld",
				"Source is World, but this graph is not running in a world. Use Source Defaults to read the asset."), Context);
			return nullptr;
		}
		Instance = World->GetParameterCollectionInstance(Collection);
		if (!Instance)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoInstance",
				"Source is World, but world '{0}' has no instance of collection '{1}'."),
				FText::FromString(World->GetName()), FText::FromString(Collection->GetName())), Context);
			return nullptr;
		}
	}

	if (Settings.Parameters.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoParameters",
			"Parameters is empty, so the attribute set has no attributes. Add entries, or click Add All Parameters."), Context);
	}

	// Validate every entry first, so one run reports every problem.
	TMap<FName, int32> FirstEntryForAttribute;
	for (int32 Index = 0; Index < Settings.Parameters.Num(); ++Index)
	{
		const FPCGUtilsMPCParameterRead& Read = Settings.Parameters[Index];
		if (Read.Parameter.IsNone())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("ParameterNone",
				"{0}: Parameter is None; pick a parameter of collection '{1}'."), EntryLabel(Index),
				FText::FromString(Collection->GetName())), Context);
			bValid = false;
		}
		else if (!Collection->GetScalarParameterByName(Read.Parameter) && !Collection->GetVectorParameterByName(Read.Parameter))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("ParameterMissing",
				"{0}: Parameter '{1}' is not a scalar or vector parameter of collection '{2}'."), EntryLabel(Index),
				FText::FromName(Read.Parameter), FText::FromString(Collection->GetName())), Context);
			bValid = false;
		}

		if (Read.AttributeName.IsNone())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("AttributeNone",
				"{0}: Attribute Name is None; it must name the attribute to write."), EntryLabel(Index)), Context);
			bValid = false;
		}
		else if (!FPCGMetadataAttributeBase::IsValidName(Read.AttributeName))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("AttributeInvalid",
				"{0}: Attribute Name '{1}' is not a valid PCG attribute name."),
				EntryLabel(Index), FText::FromName(Read.AttributeName)), Context);
			bValid = false;
		}
		else if (const int32* First = FirstEntryForAttribute.Find(Read.AttributeName))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("AttributeDuplicate",
				"{0}: Attribute Name '{1}' is already written by {2}; each entry needs its own attribute."),
				EntryLabel(Index), FText::FromName(Read.AttributeName), EntryLabel(*First)), Context);
			bValid = false;
		}
		else
		{
			FirstEntryForAttribute.Add(Read.AttributeName, Index);
		}
	}
	if (!bValid)
	{
		return nullptr;
	}

	UPCGParamData* Param = Context ? FPCGContext::NewObject_AnyThread<UPCGParamData>(Context) : NewObject<UPCGParamData>();
	UPCGMetadata* Metadata = Param->MutableMetadata();
	const int64 EntryKey = Metadata->AddEntry();

	for (const FPCGUtilsMPCParameterRead& Read : Settings.Parameters)
	{
		if (const FCollectionScalarParameter* Scalar = Collection->GetScalarParameterByName(Read.Parameter))
		{
			float Value = Scalar->DefaultValue;
			if (Instance)
			{
				Instance->GetScalarParameterValue(*Scalar, Value);
			}
			WriteMPCValue<double>(*Metadata, EntryKey, Read.AttributeName, Value);
			continue;
		}

		const FCollectionVectorParameter* Vector = Collection->GetVectorParameterByName(Read.Parameter);
		check(Vector);
		FLinearColor Value = Vector->DefaultValue;
		if (Instance)
		{
			Instance->GetVectorParameterValue(*Vector, Value);
		}
		switch (Read.VectorOutput)
		{
		case EPCGUtilsMPCVectorOutput::Vector4: WriteMPCValue(*Metadata, EntryKey, Read.AttributeName, FVector4(Value.R, Value.G, Value.B, Value.A)); break;
		case EPCGUtilsMPCVectorOutput::Vector: WriteMPCValue(*Metadata, EntryKey, Read.AttributeName, FVector(Value.R, Value.G, Value.B)); break;
		case EPCGUtilsMPCVectorOutput::Vector2: WriteMPCValue(*Metadata, EntryKey, Read.AttributeName, FVector2D(Value.R, Value.G)); break;
		case EPCGUtilsMPCVectorOutput::R: WriteMPCValue<double>(*Metadata, EntryKey, Read.AttributeName, Value.R); break;
		case EPCGUtilsMPCVectorOutput::G: WriteMPCValue<double>(*Metadata, EntryKey, Read.AttributeName, Value.G); break;
		case EPCGUtilsMPCVectorOutput::B: WriteMPCValue<double>(*Metadata, EntryKey, Read.AttributeName, Value.B); break;
		case EPCGUtilsMPCVectorOutput::A: WriteMPCValue<double>(*Metadata, EntryKey, Read.AttributeName, Value.A); break;
		}
	}
	return Param;
}

bool FPCGGetMPCValuesElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGGetMPCValuesSettings* Settings = Cast<UPCGGetMPCValuesSettings>(InSettings);
	return !Settings || Settings->Source != EPCGUtilsMPCValueSource::World;
}

bool FPCGGetMPCValuesElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetMPCValuesElement::Execute);
	check(Context);
	const UPCGGetMPCValuesSettings* Settings = Context->GetInputSettings<UPCGGetMPCValuesSettings>();
	check(Settings);

	if (Settings->Collection.IsNull())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoCollection", "Collection is not set; pick a Material Parameter Collection."), Context);
		return true;
	}
	const UMaterialParameterCollection* Collection = Settings->Collection.LoadSynchronous();
	if (!Collection)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("CollectionLoad",
			"Collection '{0}' could not be loaded; the asset may have been moved or deleted."),
			FText::FromString(Settings->Collection.ToString())), Context);
		return true;
	}

	const UWorld* World = Context->ExecutionSource.IsValid() ? Context->ExecutionSource->GetExecutionState().GetWorld() : nullptr;
	if (UPCGParamData* Param = ReadValues(Context, *Settings, Collection, World))
	{
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = Param;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
