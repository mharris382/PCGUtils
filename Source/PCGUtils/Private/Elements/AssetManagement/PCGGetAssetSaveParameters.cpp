#include "Elements/AssetManagement/PCGGetAssetSaveParameters.h"

#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Misc/PackageName.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetAssetSaveParametersElement"

// Named, not anonymous: unity builds put this file beside others with helpers of the same names.
namespace PCGGetAssetSaveParametersPrivate
{
	struct FSaveParameters
	{
		FString AssetName;
		FString AssetPath;
	};

	FSaveParameters MakeSaveParameters(const FSoftObjectPath& SourceAsset, const bool bUseActualAsset, const FString& Suffix)
	{
		FSaveParameters Parameters;
		const FString SourceAssetName = SourceAsset.GetAssetName();
		Parameters.AssetName = bUseActualAsset ? SourceAssetName : FString::Printf(TEXT("%s_%s"), *SourceAssetName, *Suffix);
		Parameters.AssetPath = SourceAsset.IsNull() ? FString() : FPackageName::GetLongPackagePath(SourceAsset.GetLongPackageName());
		return Parameters;
	}

	/**
	 * Reads the one asset reference Selector names on Data: a soft object path or a string. False, with the reason
	 * in OutError, when it is missing, of another type, or does not hold exactly one value.
	 */
	bool ReadSingleAssetReference(const UPCGData* Data, const FPCGAttributePropertyInputSelector& Selector, FSoftObjectPath& OutPath, FText& OutError)
	{
		const FText SelectorText = FText::FromString(Selector.ToString());
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector, /*bQuiet=*/true);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
		if (!Accessor || !Keys)
		{
			OutError = FText::Format(LOCTEXT("MissingInputAttribute", "Asset Attribute '{0}' was not found"), SelectorText);
			return false;
		}
		if (Keys->GetNum() != 1)
		{
			OutError = FText::Format(LOCTEXT("NotOneValue",
				"Asset Attribute '{0}' holds {1} values; Input Data mode needs exactly one per input, so select a @Data attribute"),
				SelectorText, FText::AsNumber(Keys->GetNum()));
			return false;
		}
		const uint16 Type = Accessor->GetUnderlyingType();
		if (Type == PCG::Private::MetadataTypes<FSoftObjectPath>::Id)
		{
			return Accessor->Get<FSoftObjectPath>(OutPath, 0, *Keys);
		}
		if (Type == PCG::Private::MetadataTypes<FString>::Id)
		{
			FString String;
			if (!Accessor->Get<FString>(String, 0, *Keys))
			{
				return false;
			}
			OutPath = FSoftObjectPath(String);
			return true;
		}
		OutError = FText::Format(LOCTEXT("WrongInputType", "Asset Attribute '{0}' is {1}; it must be a soft object path or a string"),
			SelectorText, StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Type));
		return false;
	}

	/** Constant and Attribute modes: one Attribute Set, one entry per asset. */
	void ExecuteAttributeSetModes(FPCGContext* Context, const UPCGGetAssetSaveParametersSettings& Settings)
	{
		UPCGParamData* OutputData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		check(OutputData && OutputData->Metadata);
		FPCGMetadataAttribute<FString>* AssetNameAttribute = OutputData->Metadata->CreateAttribute<FString>(
			Settings.AssetNameAttributeName, FString(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		FPCGMetadataAttribute<FString>* AssetPathAttribute = OutputData->Metadata->CreateAttribute<FString>(
			Settings.AssetPathAttributeName, FString(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		check(AssetNameAttribute && AssetPathAttribute);

		auto AddEntry = [&](const FSoftObjectPath& SourceAsset)
		{
			const FSaveParameters Parameters = MakeSaveParameters(SourceAsset, Settings.bUseActualAsset, Settings.Suffix);
			const PCGMetadataEntryKey OutputEntry = OutputData->Metadata->AddEntry();
			AssetNameAttribute->SetValue(OutputEntry, Parameters.AssetName);
			AssetPathAttribute->SetValue(OutputEntry, Parameters.AssetPath);
		};

		if (Settings.Source == EPCGUtilsAssetSaveParameterSource::Constant)
		{
			AddEntry(Settings.Asset.ToSoftObjectPath());
		}
		else
		{
			const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
			const UPCGParamData* InputData = Inputs.IsEmpty() ? nullptr : Cast<const UPCGParamData>(Inputs[0].Data);
			const UPCGMetadata* InputMetadata = InputData ? InputData->ConstMetadata() : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* SourceAttribute = InputMetadata
				? InputMetadata->GetConstTypedAttribute<FSoftObjectPath>(Settings.AssetAttribute)
				: nullptr;

			if (!InputData)
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("MissingParamInput", "Get Asset Save Parameters requires an Attribute Set input in Attribute mode."), Context);
			}
			else if (!SourceAttribute)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingAssetAttribute", "Input Attribute Set does not contain an FSoftObjectPath attribute named '{0}'."),
					FText::FromName(Settings.AssetAttribute)), Context);
			}
			else
			{
				const PCGMetadataEntryKey FirstInputEntry = InputMetadata->GetItemKeyCountForParent();
				const PCGMetadataEntryKey InputEntryCount = InputMetadata->GetItemCountForChild();
				for (PCGMetadataEntryKey InputEntry = FirstInputEntry; InputEntry < InputEntryCount; ++InputEntry)
				{
					AddEntry(SourceAttribute->GetValueFromItemKey(InputEntry));
				}
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	/** Input Data mode: each input, copied, with its save parameters added to its @Data. */
	void ExecuteInputDataMode(FPCGContext* Context, const UPCGGetAssetSaveParametersSettings& Settings)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
		{
			const UPCGData* InputData = Inputs[InputIndex].Data;
			if (!InputData)
			{
				continue;
			}

			FSoftObjectPath SourceAsset;
			FText ReadError;
			if (!ReadSingleAssetReference(InputData, Settings.InputAssetAttribute.CopyAndFixLast(InputData), SourceAsset, ReadError))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InputReadFailed", "Get Asset Save Parameters: input {0}: {1}; not output."),
					FText::AsNumber(InputIndex), ReadError), Context);
				continue;
			}
			if (SourceAsset.IsNull())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InputAssetEmpty",
					"Get Asset Save Parameters: input {0}: Asset Attribute '{1}' is empty; it must name an asset; not output."),
					FText::AsNumber(InputIndex), FText::FromString(Settings.InputAssetAttribute.ToString())), Context);
				continue;
			}

			UPCGData* OutputData = InputData->DuplicateData(Context, /*bInitializeMetadata=*/true);
			FPCGMetadataDomain* OutputDomain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData);
			if (!OutputDomain)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoDataDomain",
					"Get Asset Save Parameters: input {0}: {1} data cannot hold @Data attributes; not output."),
					FText::AsNumber(InputIndex), FText::FromString(InputData->GetClass()->GetName())), Context);
				continue;
			}

			const FSaveParameters Parameters = MakeSaveParameters(SourceAsset, Settings.bUseActualAsset, Settings.Suffix);
			PCGUtilsDataDomain::WriteValue<FString>(*OutputDomain, Settings.AssetNameAttributeName, Parameters.AssetName);
			PCGUtilsDataDomain::WriteValue<FString>(*OutputDomain, Settings.AssetPathAttributeName, Parameters.AssetPath);

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			Output.Data = OutputData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
}

#if WITH_EDITOR
FText UPCGGetAssetSaveParametersSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Asset Save Parameters");
}

FText UPCGGetAssetSaveParametersSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Derives the AssetName and AssetPath strings used by PCG asset-saving nodes from an existing asset reference. "
		"By default, the output name is {original name}_{suffix}; even an empty suffix leaves the underscore in place. "
		"Enable Use Actual Asset to output the original name and allow the source asset to be modified or replaced. "
		"Input Data mode does this for every input at once, writing both to each input's @Data for DynMesh | Save Static Mesh.");
}

EPCGChangeType UPCGGetAssetSaveParametersSettings::GetChangeTypeForProperty(
	FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(UPCGGetAssetSaveParametersSettings, Source))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGGetAssetSaveParametersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (Source == EPCGUtilsAssetSaveParameterSource::Attribute)
	{
		Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Param, false, false).SetRequiredPin();
	}
	else if (Source == EPCGUtilsAssetSaveParameterSource::InputData)
	{
		Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
			LOCTEXT("InputDataTooltip", "Data that each name one asset in Asset Attribute, such as DynMeshes made by DynMesh | From Static Mesh."))
			.SetRequiredPin();
	}

	return Pins;
}

TArray<FPCGPinProperties> UPCGGetAssetSaveParametersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (Source == EPCGUtilsAssetSaveParameterSource::InputData)
	{
		Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
			LOCTEXT("InputDataOutTooltip", "The inputs, each with its save parameters added to its @Data."));
	}
	else
	{
		Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param, false, false);
	}
	return Pins;
}

FPCGDataTypeIdentifier UPCGGetAssetSaveParametersSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (InPin && InPin->IsOutputPin() && Source == EPCGUtilsAssetSaveParameterSource::InputData)
	{
		const FPCGDataTypeIdentifier InputTypes = GetTypeUnionIDOfIncidentEdges(PCGPinConstants::DefaultInputLabel);
		return InputTypes.IsValid() ? InputTypes : FPCGDataTypeIdentifier{EPCGDataType::Any};
	}
	return Super::GetCurrentPinTypesID(InPin);
}

FPCGElementPtr UPCGGetAssetSaveParametersSettings::CreateElement() const
{
	return MakeShared<FPCGGetAssetSaveParametersElement>();
}

bool FPCGGetAssetSaveParametersElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetAssetSaveParametersElement::ExecuteInternal);
	using namespace PCGGetAssetSaveParametersPrivate;

	check(InContext);
	const UPCGGetAssetSaveParametersSettings* Settings = InContext->GetInputSettings<UPCGGetAssetSaveParametersSettings>();
	check(Settings);

	if (Settings->AssetNameAttributeName.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoNameAttribute", "Get Asset Save Parameters: Asset Name Attribute Name is None; it names the attribute that receives the asset name."), InContext);
		return true;
	}
	if (Settings->AssetPathAttributeName.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoPathAttribute", "Get Asset Save Parameters: Asset Path Attribute Name is None; it names the attribute that receives the asset folder."), InContext);
		return true;
	}
	if (Settings->AssetNameAttributeName == Settings->AssetPathAttributeName)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SameAttribute",
			"Get Asset Save Parameters: Asset Name Attribute Name and Asset Path Attribute Name are both '{0}'; they must differ."),
			FText::FromName(Settings->AssetNameAttributeName)), InContext);
		return true;
	}

	if (Settings->Source == EPCGUtilsAssetSaveParameterSource::InputData)
	{
		ExecuteInputDataMode(InContext, *Settings);
	}
	else
	{
		ExecuteAttributeSetModes(InContext, *Settings);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
