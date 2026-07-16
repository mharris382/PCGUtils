#include "Elements/PCGGetOverrideGraphSets.h"

#include "Data/PCGPointData.h"
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
		"Routes point data sets by override graph attributes in the selected metadata domain. Data sets without an override go to NoOverride. "
		"Data sets with a non-null override go to SingleOverride when all overrides resolve to one graph, or MultipleOverrides when more than one unique graph is present. "
		"UniqueOverrideGraphs outputs an Attribute Set containing one entry per unique override graph. "
		"Element-domain searches inspect every point and are only performed when requested.");
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

	TArray<bool> bHasNonNullOverride;
	bHasNonNullOverride.Init(false, Inputs.Num());

	TSet<FSoftObjectPath> UniqueOverridePaths;
	TArray<FSoftObjectPath> UniqueOverridePathsInOrder;
	const bool bCheckDataDomain = Settings->TargetDomain != EPCGMetadataDomainTarget::Elements;
	const bool bCheckElementsDomain = Settings->TargetDomain != EPCGMetadataDomainTarget::Data;

	auto AddOverridePath = [&UniqueOverridePaths, &UniqueOverridePathsInOrder](
		const FSoftObjectPath& OverridePath)
	{
		if (!OverridePath.IsNull() && !UniqueOverridePaths.Contains(OverridePath))
		{
			UniqueOverridePaths.Add(OverridePath);
			UniqueOverridePathsInOrder.Add(OverridePath);
		}
	};

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];

		if (!Input.Data)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NullInputData", "GetOverrideGraphSets: input data is null; routing to NoOverride."));
			continue;
		}

		const UPCGMetadata* Metadata = Input.Data->ConstMetadata();
		if (!Metadata)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(
				LOCTEXT("MissingMetadata", "GetOverrideGraphSets: input data has no metadata containing attribute '{0}'; routing to NoOverride."),
				FText::FromName(AttributeName)));
			continue;
		}

		if (bCheckDataDomain)
		{
			const FPCGMetadataDomain* DataDomain =
				Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
			const FPCGMetadataAttribute<FSoftObjectPath>* DataGraphAttribute =
				DataDomain ? DataDomain->GetConstTypedAttribute<FSoftObjectPath>(AttributeName) : nullptr;
			if (DataGraphAttribute)
			{
				const FSoftObjectPath OverridePath =
					DataGraphAttribute->GetValueFromItemKey(PCGInvalidEntryKey);
				if (!OverridePath.IsNull())
				{
					bHasNonNullOverride[InputIndex] = true;
					AddOverridePath(OverridePath);
				}
			}
		}

		// Element-domain lookup is intentionally isolated behind this setting because
		// every point metadata entry must be visited.
		if (bCheckElementsDomain)
		{
			const FPCGMetadataDomain* ElementsDomain =
				Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
			const FPCGMetadataAttribute<FSoftObjectPath>* ElementsGraphAttribute =
				ElementsDomain ? ElementsDomain->GetConstTypedAttribute<FSoftObjectPath>(AttributeName) : nullptr;
			const UPCGPointData* PointData = Cast<UPCGPointData>(Input.Data);

			if (ElementsGraphAttribute && PointData)
			{
				for (const FPCGPoint& Point : PointData->GetPoints())
				{
					if (Point.MetadataEntry == PCGInvalidEntryKey)
					{
						continue;
					}

					const FSoftObjectPath OverridePath =
						ElementsGraphAttribute->GetValueFromItemKey(Point.MetadataEntry);
					if (!OverridePath.IsNull())
					{
						bHasNonNullOverride[InputIndex] = true;
						AddOverridePath(OverridePath);
					}
				}
			}
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
