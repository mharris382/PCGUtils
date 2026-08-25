#include "Elements/AssetManagement/PCGGetAssetSaveParameters.h"

#include "Misc/PackageName.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetAssetSaveParametersElement"

namespace PCGGetAssetSaveParametersConstants
{
	const FName AssetNameAttribute = TEXT("AssetName");
	const FName AssetPathAttribute = TEXT("AssetPath");
}

namespace
{
	void AddSaveParameters(
		UPCGMetadata* OutputMetadata,
		FPCGMetadataAttribute<FString>* AssetNameAttribute,
		FPCGMetadataAttribute<FString>* AssetPathAttribute,
		const FSoftObjectPath& SourceAsset,
		const FString& Suffix)
	{
		check(OutputMetadata && AssetNameAttribute && AssetPathAttribute);

		const PCGMetadataEntryKey OutputEntry = OutputMetadata->AddEntry();
		const FString OutputAssetName = FString::Printf(TEXT("%s_%s"), *SourceAsset.GetAssetName(), *Suffix);
		const FString OutputAssetPath = SourceAsset.IsNull()
			? FString()
			: FPackageName::GetLongPackagePath(SourceAsset.GetLongPackageName());

		AssetNameAttribute->SetValue(OutputEntry, OutputAssetName);
		AssetPathAttribute->SetValue(OutputEntry, OutputAssetPath);
	}

	UPCGParamData* CreateOutputData(FPCGContext* Context)
	{
		UPCGParamData* OutputData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		check(OutputData && OutputData->Metadata);

		OutputData->Metadata->CreateAttribute<FString>(
			PCGGetAssetSaveParametersConstants::AssetNameAttribute,
			FString(),
			/*bAllowsInterpolation=*/false,
			/*bOverrideParent=*/false);
		OutputData->Metadata->CreateAttribute<FString>(
			PCGGetAssetSaveParametersConstants::AssetPathAttribute,
			FString(),
			/*bAllowsInterpolation=*/false,
			/*bOverrideParent=*/false);

		return OutputData;
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
		"The output name is always {original name}_{suffix}; even an empty suffix leaves the underscore in place.");
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

	return Pins;
}

TArray<FPCGPinProperties> UPCGGetAssetSaveParametersSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param, false, false);
	return Pins;
}

FPCGElementPtr UPCGGetAssetSaveParametersSettings::CreateElement() const
{
	return MakeShared<FPCGGetAssetSaveParametersElement>();
}

bool FPCGGetAssetSaveParametersElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetAssetSaveParametersElement::ExecuteInternal);

	check(InContext);
	FPCGContext* Context = InContext;
	const UPCGGetAssetSaveParametersSettings* Settings =
		Context->GetInputSettings<UPCGGetAssetSaveParametersSettings>();
	check(Settings);

	UPCGParamData* OutputData = CreateOutputData(Context);
	FPCGMetadataAttribute<FString>* AssetNameAttribute =
		OutputData->Metadata->GetMutableTypedAttribute<FString>(
			PCGGetAssetSaveParametersConstants::AssetNameAttribute);
	FPCGMetadataAttribute<FString>* AssetPathAttribute =
		OutputData->Metadata->GetMutableTypedAttribute<FString>(
			PCGGetAssetSaveParametersConstants::AssetPathAttribute);
	check(AssetNameAttribute && AssetPathAttribute);

	if (Settings->Source == EPCGUtilsAssetSaveParameterSource::Constant)
	{
		AddSaveParameters(
			OutputData->Metadata,
			AssetNameAttribute,
			AssetPathAttribute,
			Settings->Asset.ToSoftObjectPath(),
			Settings->Suffix);
	}
	else
	{
		const TArray<FPCGTaggedData> Inputs =
			Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		const UPCGParamData* InputData = Inputs.IsEmpty()
			? nullptr
			: Cast<const UPCGParamData>(Inputs[0].Data);
		const UPCGMetadata* InputMetadata = InputData ? InputData->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<FSoftObjectPath>* SourceAttribute = InputMetadata
			? InputMetadata->GetConstTypedAttribute<FSoftObjectPath>(Settings->AssetAttribute)
			: nullptr;

		if (!InputData)
		{
			PCGE_LOG(Error, GraphAndLog,
				LOCTEXT("MissingParamInput", "Get Asset Save Parameters requires an Attribute Set input in Attribute mode."));
		}
		else if (!SourceAttribute)
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(
				LOCTEXT("MissingAssetAttribute",
					"Input Attribute Set does not contain an FSoftObjectPath attribute named '{0}'."),
				FText::FromName(Settings->AssetAttribute)));
		}
		else
		{
			const PCGMetadataEntryKey FirstInputEntry = InputMetadata->GetItemKeyCountForParent();
			const PCGMetadataEntryKey InputEntryCount = InputMetadata->GetItemCountForChild();
			for (PCGMetadataEntryKey InputEntry = FirstInputEntry; InputEntry < InputEntryCount; ++InputEntry)
			{
				AddSaveParameters(
					OutputData->Metadata,
					AssetNameAttribute,
					AssetPathAttribute,
					SourceAttribute->GetValueFromItemKey(InputEntry),
					Settings->Suffix);
			}
		}
	}

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutputData;
	Output.Pin = PCGPinConstants::DefaultOutputLabel;

	return true;
}

#undef LOCTEXT_NAMESPACE
