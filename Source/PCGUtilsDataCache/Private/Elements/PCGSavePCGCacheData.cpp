#include "Elements/PCGSavePCGCacheData.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGDataCacheComponent.h"
#include "PCGUtilsDataCacheExporter.h"
#include "PCGUtilsDataCacheHelpers.h"

#if WITH_EDITOR
#include "PCGAssetExporter.h"
#include "PCGAssetExporterUtils.h"
#endif

#define LOCTEXT_NAMESPACE "PCGSavePCGCacheData"

UPCGSavePCGCacheDataSettings::UPCGSavePCGCacheDataSettings() = default;

#if WITH_EDITOR
FText UPCGSavePCGCacheDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Save PCG Cache Data");
}

FText UPCGSavePCGCacheDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Saves the complete input data collection to this actor's deterministic PCG data cache asset. The input data passes through unchanged.");
}
#endif

TArray<FPCGPinProperties> UPCGSavePCGCacheDataSettings::InputPinProperties() const
{
	return { FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any) };
}

TArray<FPCGPinProperties> UPCGSavePCGCacheDataSettings::OutputPinProperties() const
{
	return { FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any) };
}

FPCGElementPtr UPCGSavePCGCacheDataSettings::CreateElement() const
{
	return MakeShared<FPCGSavePCGCacheDataElement>();
}

bool FPCGSavePCGCacheDataElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGSavePCGCacheDataSettings* Settings = Context->GetInputSettings<UPCGSavePCGCacheDataSettings>();
	check(Settings);

	FPCGDataCollection CollectionToSave;
	for (const FPCGTaggedData& Input : Context->InputData.TaggedData)
	{
		if (Input.Pin == PCGPinConstants::DefaultInputLabel)
		{
			CollectionToSave.TaggedData.Add(Input);
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}

#if !WITH_EDITOR
	PCGE_LOG_C(Warning, GraphAndLog, Context, LOCTEXT("PackagedNoOp", "Save PCG Cache Data is editor-only and is a no-op in packaged builds."));
	return true;
#else
	const IPCGGraphExecutionSource* ExecutionSource = Context->ExecutionSource.Get();
	AActor* SourceActor = ExecutionSource ? ExecutionSource->GetExecutionState().GetTypedTarget<AActor>() : nullptr;
	if (!SourceActor)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("NoSourceActor", "Unable to resolve the source actor for this cache save."));
		return true;
	}

	bool bFoundMultiple = false;
	UPCGDataCacheComponent* CacheComponent = PCGUtilsDataCacheHelpers::FindSingleCacheComponent(SourceActor, bFoundMultiple);
	if (bFoundMultiple)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("MultipleComponents", "The source actor has multiple PCG Data Cache components; exactly one is required."));
		return true;
	}
	if (!CacheComponent)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("MissingComponent", "The source actor does not have a PCG Data Cache component."));
		return true;
	}

	if (!CacheComponent->UsesCache(Settings->SaveTargetCache))
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, LOCTEXT("DisabledCache", "The selected cache target is disabled on the PCG Data Cache component."));
		return true;
	}

	FString PackageName;
	FSoftObjectPath ObjectPath;
	FText PathError;
	if (!PCGUtilsDataCacheHelpers::BuildAssetPath(Settings->SaveTargetCache, CacheComponent->CacheSaveName, PackageName, ObjectPath, PathError))
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("BadPath", "Cannot save the PCG cache: {0}"), PathError));
		return true;
	}

	UPCGUtilsDataCacheExporter* Exporter = NewObject<UPCGUtilsDataCacheExporter>();
	Exporter->Data = MoveTemp(CollectionToSave);
	Exporter->AssetDescription = CacheComponent->CacheDescription.ToString();
	Exporter->AssetColor = CacheComponent->CacheColor;

	FPCGAssetExporterParameters Parameters;
	Parameters.bOpenSaveDialog = false;
	Parameters.bSaveOnExportEnded = true;
	Parameters.AssetPath = FPackageName::GetLongPackagePath(PackageName);
	Parameters.AssetName = CacheComponent->CacheSaveName.ToString();

	if (!UPCGAssetExporterUtils::CreateAsset(Exporter, Parameters, Context))
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("SaveFailed", "Failed to save PCG cache asset '{0}'."), FText::FromString(ObjectPath.ToString())));
	}
	else
	{
		PCGE_LOG_C(Log, GraphAndLog, Context, FText::Format(LOCTEXT("SaveSucceeded", "Saved PCG cache asset '{0}'."), FText::FromString(ObjectPath.ToString())));
	}

	return true;
#endif
}

#undef LOCTEXT_NAMESPACE
