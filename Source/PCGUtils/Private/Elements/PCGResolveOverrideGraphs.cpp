#include "Elements/PCGResolveOverrideGraphs.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGResolveOverrideGraphs)

#define LOCTEXT_NAMESPACE "PCGResolveOverrideGraphsElement"

DEFINE_LOG_CATEGORY_STATIC(LogPCGResolveOverrideGraphs, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

#if WITH_EDITOR
FText UPCGResolveOverrideGraphsSettings::GetNodeTooltipText() const
{
    return LOCTEXT("Tooltip",
        "Reads @Data source attributes from incoming data, resolves each to an "
        "IPCGOverrideGraphProvider, and stamps resolved override graphs back into "
        "@Data. Sources are resolved left-to-right, highest priority first. "
        "An optional fallback DataAsset or DataTable is used last.");
}
#endif

TArray<FPCGPinProperties> UPCGResolveOverrideGraphsSettings::InputPinProperties() const
{
    TArray<FPCGPinProperties> Props;
    Props.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any,
        /*bAllowMultipleData=*/true, /*bAllowMultipleConnections=*/true);
    return Props;
}

TArray<FPCGPinProperties> UPCGResolveOverrideGraphsSettings::OutputPinProperties() const
{
    TArray<FPCGPinProperties> Props;
    Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any,
        /*bAllowMultipleData=*/true, false);
    return Props;
}

FPCGElementPtr UPCGResolveOverrideGraphsSettings::CreateElement() const
{
    return MakeShared<FPCGResolveOverrideGraphsElement>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Element
// ─────────────────────────────────────────────────────────────────────────────

bool FPCGResolveOverrideGraphsElement::ExecuteInternal(FPCGContext* Context) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FPCGResolveOverrideGraphsElement::ExecuteInternal);

    check(Context);

    const UPCGResolveOverrideGraphsSettings* Settings =Context->GetInputSettings<UPCGResolveOverrideGraphsSettings>();
    check(Settings);

    // Parse comma-separated settings strings once — returns array directly.
    TArray<FString> SourceNamesStr =PCGHelpers::GetStringArrayFromCommaSeparatedList(Settings->SourceAttributeNames);
    TArray<FString> ContractNamesStr =PCGHelpers::GetStringArrayFromCommaSeparatedList(Settings->RequestedContracts);

    TArray<FName> SourceNames;
    for (const FString& S : SourceNamesStr)
    {
        SourceNames.Add(FName(S.TrimStartAndEnd()));
    }

    TArray<FName> RequestedContracts;
    for (const FString& S : ContractNamesStr)
    {
        RequestedContracts.Add(FName(S.TrimStartAndEnd()));
    }

    // Build fallback entries once — same for all incoming data.
    TArray<FPCGNamedOverrideGraph> FallbackEntries;
    if (Settings->bUseFallback)
    {
        if (Settings->FallbackMode == EPCGOverrideFallbackSourceMode::DataAsset
            && Settings->FallbackDataAsset)
        {
            FallbackEntries = UPCGOverrideGraphLibrary::GetOverrideEntriesFromObject(
                Settings->FallbackDataAsset, RequestedContracts);
        }
        else if (Settings->FallbackMode == EPCGOverrideFallbackSourceMode::DataTable
            && Settings->FallbackDataTable)
        {
            FallbackEntries = UPCGOverrideGraphLibrary::GetOverrideEntriesFromDataTable(
                Settings->FallbackDataTable, RequestedContracts);
        }
    }

    TArray<FPCGTaggedData> Inputs =
        Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

    for (FPCGTaggedData& TaggedData : Inputs)
    {
        const UPCGData* Data = TaggedData.Data;
        if (!Data)
        {
            Context->OutputData.TaggedData.Add(TaggedData);
            continue;
        }

        const UPCGMetadata* Metadata = Data->ConstMetadata();

        // Build provider stack from @Data attributes on this data item.
        TArray<UObject*> Providers;
        for (const FName& SourceName : SourceNames)
        {
            UObject* Provider = ResolveProviderFromMetadata(Metadata, SourceName);
            if (Provider)
            {
                Providers.Add(Provider);
            }
            else if (!Settings->bSuppressMissingSourceWarning)
            {
                UE_LOG(LogPCGResolveOverrideGraphs, Warning,
                    TEXT("ResolveOverrideGraphs: source attribute '%s' not found or "
                        "not an IPCGOverrideGraphProvider. Enable bSuppressMissingSourceWarning "
                        "to silence this for optional sources."),
                    *SourceName.ToString());
            }
        }

        // Resolve provider stack — first active entry per contract wins.
        TArray<FPCGNamedOverrideGraph> Resolved =
            UPCGOverrideGraphLibrary::ResolveOverrideStackFromObjects(
                Providers, RequestedContracts);

        // Merge fallback entries — only fills contracts not already resolved.
        for (const FPCGNamedOverrideGraph& FallbackEntry : FallbackEntries)
        {
            if (!FallbackEntry.IsValid() || !FallbackEntry.Override.IsActive())
            {
                continue;
            }
            const bool bAlreadyResolved = Resolved.ContainsByPredicate(
                [&](const FPCGNamedOverrideGraph& E)
                {
                    return E.ContractName == FallbackEntry.ContractName;
                });
            if (!bAlreadyResolved)
            {
                Resolved.Add(FallbackEntry);
            }
        }

        if (Resolved.IsEmpty())
        {
            Context->OutputData.TaggedData.Add(TaggedData);
            continue;
        }

        // Duplicate data so we can write metadata without mutating the input.
        UPCGData* OutputData = Data->DuplicateData(Context);
        if (!OutputData)
        {
            Context->OutputData.TaggedData.Add(TaggedData);
            continue;
        }

        // Get the @Data metadata domain directly.
        FPCGMetadataDomain* DataDomain =
            OutputData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);

        if (!DataDomain)
        {
            PCGE_LOG(Warning, GraphAndLog,
                LOCTEXT("NoDataDomain",
                    "ResolveOverrideGraphs: output data has no @Data metadata domain. "
                    "Passing data through unchanged."));
            Context->OutputData.TaggedData.Add(TaggedData);
            continue;
        }

        // Stamp resolved entries into the @Data domain.
        UPCGOverrideGraphLibrary::StampOverrideEntriesToMetadataDomain(Resolved, DataDomain);

        FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(TaggedData);
        Output.Data = OutputData;
    }

    return true;
}

UObject* FPCGResolveOverrideGraphsElement::ResolveProviderFromMetadata(
    const UPCGMetadata* Metadata,
    FName AttributeName)
{
    if (!Metadata)
    {
        return nullptr;
    }

    const FPCGAttributeIdentifier Identifier(AttributeName, PCGMetadataDomainID::Data);

    const FPCGMetadataAttribute<FSoftObjectPath>* Attribute =
        Metadata->GetConstTypedAttribute<FSoftObjectPath>(Identifier);

    if (!Attribute)
    {
        return nullptr;
    }

    const FSoftObjectPath Path = Attribute->GetValue(PCGInvalidEntryKey);
    if (Path.IsNull())
    {
        return nullptr;
    }

    UObject* Object = Path.TryLoad();
    if (!Object)
    {
        return nullptr;
    }

    // Accept DataTables directly — GetOverrideEntriesFromObject handles them
    // via the DataTable path without requiring the interface.
    if (!Object->IsA<UDataTable>() && !Object->Implements<UPCGOverrideGraphProvider>())
    {
        return nullptr;
    }

    return Object;
}

#undef LOCTEXT_NAMESPACE