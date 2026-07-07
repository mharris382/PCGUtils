#include "Elements/PCGGetOverrideGraphSets.h"

#include "PCGParamData.h"
#include "PCGPin.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"

#define LOCTEXT_NAMESPACE "PCGGetOverrideGraphSetsElement"

namespace PCGGetOverrideGraphSetsConstants
{
	static const FName NoOverridePin = FName(TEXT("NoOverride"));
	static const FName SingleOverridePin = FName(TEXT("SingleOverride"));
	static const FName MultipleOverridesPin = FName(TEXT("MultipleOverrides"));
	static const FName UniqueOverrideGraphsPin = FName(TEXT("UniqueOverrideGraphs"));
}

#if WITH_EDITOR
FText UPCGGetOverrideGraphSetsSettings::GetDefaultNodeTitle() const
{
	return NSLOCTEXT("PCGGetOverrideGraphSetsElement", "NodeTitle", "Get Override Graph Sets");
}

FText UPCGGetOverrideGraphSetsSettings::GetNodeTooltipText() const
{
	return NSLOCTEXT("PCGGetOverrideGraphSetsElement", "NodeTooltip",
		"Routes point data sets by their @Data override graph attribute. Data sets without an override go to NoOverride. "
		"Data sets with a non-null override go to SingleOverride when all overrides resolve to one graph, or MultipleOverrides when more than one unique graph is present. "
		"UniqueOverrideGraphs outputs an Attribute Set containing one entry per unique override graph.");
}
#endif

TArray<FPCGPinProperties> UPCGGetOverrideGraphSetsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true).SetRequiredPin();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGGetOverrideGraphSetsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGGetOverrideGraphSetsConstants::NoOverridePin, EPCGDataType::Point, true, true);
	PinProperties.Emplace(PCGGetOverrideGraphSetsConstants::SingleOverridePin, EPCGDataType::Point, true, true);
	PinProperties.Emplace(PCGGetOverrideGraphSetsConstants::MultipleOverridesPin, EPCGDataType::Point, true, true);
	PinProperties.Emplace(PCGGetOverrideGraphSetsConstants::UniqueOverrideGraphsPin, EPCGDataType::Param, true, true);
	return PinProperties;
}

FPCGElementPtr UPCGGetOverrideGraphSetsSettings::CreateElement() const
{
	return MakeShared<FPCGGetOverrideGraphSetsElement>();
}

bool FPCGGetOverrideGraphSetsElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetOverrideGraphSetsElement::ExecuteInternal);

	check(InContext);
	FPCGContext* Context = InContext;

	const UPCGGetOverrideGraphSetsSettings* Settings = InContext->GetInputSettings<UPCGGetOverrideGraphSetsSettings>();
	check(Settings);

	const FName AttributeName = Settings->OverrideGraphAttributeName;
	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<FSoftObjectPath> OverridePaths;
	OverridePaths.SetNum(Inputs.Num());

	TArray<bool> bHasNonNullOverride;
	bHasNonNullOverride.Init(false, Inputs.Num());

	TSet<FSoftObjectPath> UniqueOverridePaths;
	TArray<FSoftObjectPath> UniqueOverridePathsInOrder;

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];

		if (!Input.Data)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NullInputData", "GetOverrideGraphSets: input data is null; routing to NoOverride."));
			continue;
		}

		const UPCGMetadata* Metadata = Input.Data->ConstMetadata();
		const FPCGMetadataDomain* DataDomain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!DataDomain)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				LOCTEXT("MissingDataDomain", "GetOverrideGraphSets: input data has no @Data metadata domain containing attribute '{0}'; routing to NoOverride."),
				FText::FromName(AttributeName)));
			continue;
		}

		const FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute = DataDomain->GetConstTypedAttribute<FSoftObjectPath>(AttributeName);
		if (!GraphAttribute)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				LOCTEXT("MissingOverrideAttribute", "GetOverrideGraphSets: @Data attribute '{0}' is missing or is not a Soft Object Path; routing to NoOverride."),
				FText::FromName(AttributeName)));
			continue;
		}

		const FSoftObjectPath OverridePath = GraphAttribute->GetValueFromItemKey(PCGInvalidEntryKey);
		if (OverridePath.IsNull())
		{
			continue;
		}

		OverridePaths[InputIndex] = OverridePath;
		bHasNonNullOverride[InputIndex] = true;
		if (!UniqueOverridePaths.Contains(OverridePath))
		{
			UniqueOverridePaths.Add(OverridePath);
			UniqueOverridePathsInOrder.Add(OverridePath);
		}
	}

	const FName OverrideOutputPin = UniqueOverridePaths.Num() > 1
		? PCGGetOverrideGraphSetsConstants::MultipleOverridesPin
		: PCGGetOverrideGraphSetsConstants::SingleOverridePin;

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Pin = bHasNonNullOverride[InputIndex]
			? OverrideOutputPin
			: PCGGetOverrideGraphSetsConstants::NoOverridePin;
	}

	UPCGParamData* UniqueOverrideGraphsData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	check(UniqueOverrideGraphsData && UniqueOverrideGraphsData->Metadata);

	FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
		UniqueOverrideGraphsData->Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			AttributeName,
			FSoftObjectPath(),
			/*bAllowsInterpolation=*/false,
			/*bOverrideParent=*/false,
			/*bOverwriteIfTypeMismatch=*/true);

	if (GraphAttribute)
	{
		for (const FSoftObjectPath& OverridePath : UniqueOverridePathsInOrder)
		{
			const PCGMetadataEntryKey EntryKey = UniqueOverrideGraphsData->Metadata->AddEntry();
			GraphAttribute->SetValue(EntryKey, OverridePath);
		}
	}

	FPCGTaggedData& UniqueOverrideGraphsOutput = InContext->OutputData.TaggedData.Emplace_GetRef();
	UniqueOverrideGraphsOutput.Data = UniqueOverrideGraphsData;
	UniqueOverrideGraphsOutput.Pin = PCGGetOverrideGraphSetsConstants::UniqueOverrideGraphsPin;

	return true;
}

#undef LOCTEXT_NAMESPACE
