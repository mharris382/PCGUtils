#include "OverrideGraphs.h"
#include "Engine/DataTable.h"
#include "Metadata/PCGMetadata.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCGOverrideGraphs, Log, All);

// -----------------------------------------------------------------------------
// UPCGOverrideGraphLibrary
// -----------------------------------------------------------------------------

TArray<FPCGNamedOverrideGraph> UPCGOverrideGraphLibrary::GetOverrideEntries(
    TScriptInterface<IPCGOverrideGraphProvider> Provider,
    const TArray<FName>& RequestedContracts)
{
    if (!Provider)
    {
        return {};
    }
    return GetOverrideEntriesFromObject(Provider.GetObject(), RequestedContracts);
}

TArray<FPCGNamedOverrideGraph> UPCGOverrideGraphLibrary::GetOverrideEntriesFromObject(
    UObject* Object,
    const TArray<FName>& RequestedContracts)
{
    if (!Object)
    {
        return {};
    }

    // DataTable path — no fallback concept, row name is the full contract.
    if (const UDataTable* DataTable = Cast<UDataTable>(Object))
    {
        return GetOverrideEntriesFromDataTable(DataTable, RequestedContracts);
    }

    if (!Object->Implements<UPCGOverrideGraphProvider>())
    {
        return {};
    }

    // Get this object's own entries first.
    TArray<FPCGNamedOverrideGraph> OwnEntries =
        IPCGOverrideGraphProvider::Execute_GetOverrideGraphEntries(Object);

    if (!RequestedContracts.IsEmpty())
    {
        OwnEntries = OwnEntries.FilterByPredicate(
            [&](const FPCGNamedOverrideGraph& E)
            {
                return RequestedContracts.Contains(E.ContractName);
            });
    }

    // Resolve fallback — single depth only, no further recursion.
    UObject* FallbackSource =
        IPCGOverrideGraphProvider::Execute_GetOverrideGraphFallbackSource(Object);

    if (!FallbackSource || FallbackSource == Object)
    {
        return OwnEntries;
    }

    // Get fallback entries — deliberately NOT calling GetOverrideEntriesFromObject
    // recursively here. We call the raw interface/DT resolution directly to
    // enforce the single-depth limit.
    TArray<FPCGNamedOverrideGraph> FallbackEntries;
    if (const UDataTable* FallbackTable = Cast<UDataTable>(FallbackSource))
    {
        FallbackEntries = GetOverrideEntriesFromDataTable(FallbackTable, RequestedContracts);
    }
    else if (FallbackSource->Implements<UPCGOverrideGraphProvider>())
    {
        FallbackEntries = IPCGOverrideGraphProvider::Execute_GetOverrideGraphEntries(FallbackSource);
        if (!RequestedContracts.IsEmpty())
        {
            FallbackEntries = FallbackEntries.FilterByPredicate(
                [&](const FPCGNamedOverrideGraph& E)
                {
                    return RequestedContracts.Contains(E.ContractName);
                });
        }
    }

    // Merge — own entries win, fallback fills unresolved contracts.
    // Build a set of already-resolved contract names for O(1) lookup.
    TSet<FName> ResolvedContracts;
    for (const FPCGNamedOverrideGraph& Entry : OwnEntries)
    {
        if (Entry.IsValid() && Entry.Override.IsActive())
        {
            ResolvedContracts.Add(Entry.ContractName);
        }
    }

    for (const FPCGNamedOverrideGraph& FallbackEntry : FallbackEntries)
    {
        if (FallbackEntry.IsValid() && !ResolvedContracts.Contains(FallbackEntry.ContractName))
        {
            OwnEntries.Add(FallbackEntry);
        }
    }

    return OwnEntries;
}

TArray<FPCGNamedOverrideGraph> UPCGOverrideGraphLibrary::GetOverrideEntriesFromDataTable(
    const UDataTable* DataTable,
    const TArray<FName>& RequestedContracts)
{
    TArray<FPCGNamedOverrideGraph> Result;
    if (!DataTable)
    {
        return Result;
    }

    if (DataTable->GetRowStruct() != FPCGOverrideGraphTableRow::StaticStruct())
    {
        UE_LOG(LogPCGOverrideGraphs, Warning,
            TEXT("GetOverrideEntriesFromDataTable: DataTable '%s' row type is not "
                "FPCGOverrideGraphTableRow."),
            *DataTable->GetName());
        return Result;
    }

    DataTable->ForeachRow<FPCGOverrideGraphTableRow>(
        TEXT("PCGOverrideGraphLibrary"),
        [&](const FName& RowName, const FPCGOverrideGraphTableRow& Row)
        {
            if (!RequestedContracts.IsEmpty() && !RequestedContracts.Contains(RowName))
            {
                return;
            }
            FPCGNamedOverrideGraph Entry;
            Entry.ContractName = RowName;
            Entry.Override = Row.Override;
            Result.Add(Entry);
        });

    return Result;
}

TArray<FPCGNamedOverrideGraph> UPCGOverrideGraphLibrary::ResolveOverrideStack(
    const TArray<TScriptInterface<IPCGOverrideGraphProvider>>& Providers,
    const TArray<FName>& RequestedContracts)
{
    TArray<UObject*> Objects;
    Objects.Reserve(Providers.Num());
    for (const TScriptInterface<IPCGOverrideGraphProvider>& P : Providers)
    {
        if (P)
        {
            Objects.Add(P.GetObject());
        }
    }
    return ResolveOverrideStackFromObjects(Objects, RequestedContracts);
}

TArray<FPCGNamedOverrideGraph> UPCGOverrideGraphLibrary::ResolveOverrideStackFromObjects(
    const TArray<UObject*>& Providers,
    const TArray<FName>& RequestedContracts)
{
    // First active entry per contract name wins.
    TMap<FName, FPCGNamedOverrideGraph> Resolved;

    for (UObject* Object : Providers)
    {
        TArray<FPCGNamedOverrideGraph> Entries =
            GetOverrideEntriesFromObject(Object, RequestedContracts);

        for (const FPCGNamedOverrideGraph& Entry : Entries)
        {
            if (!Entry.IsValid() || !Entry.Override.IsActive())
            {
                continue;
            }
            if (!Resolved.Contains(Entry.ContractName))
            {
                Resolved.Add(Entry.ContractName, Entry);
            }
        }
    }

    TArray<FPCGNamedOverrideGraph> Result;
    Resolved.GenerateValueArray(Result);
    Result.Sort([](const FPCGNamedOverrideGraph& A, const FPCGNamedOverrideGraph& B)
        {
            return A.ContractName.LexicalLess(B.ContractName);
        });
    return Result;
}

void UPCGOverrideGraphLibrary::StampOverrideEntriesToMetadata(
    const TArray<FPCGNamedOverrideGraph>& Entries,
    UPCGMetadata* Metadata)
{
    if (!Metadata)
    {
        return;
    }

    for (const FPCGNamedOverrideGraph& Entry : Entries)
    {
        if (!Entry.IsValid() || !Entry.Override.bUseGraph)
        {
            continue;
        }

        const FString Prefix = Entry.ContractName.ToString();



        const FPCGAttributeIdentifier GraphAttr(
            FName( Prefix ),
            PCGMetadataDomainID::Data);


        if (FPCGMetadataAttribute<FSoftObjectPath>* Attr = Metadata->FindOrCreateAttribute<FSoftObjectPath>(GraphAttr, FSoftObjectPath(),
                /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
                /*bOverwriteIfTypeMismatch=*/false))
        {
            Attr->SetValue(PCGInvalidEntryKey, Entry.Override.Graph.Get());
        }
    }
}


void UPCGOverrideGraphLibrary::StampOverrideEntriesToMetadataDomain(
    const TArray<FPCGNamedOverrideGraph>& Entries,
    FPCGMetadataDomain* DataDomain)
{
    if (!DataDomain)
    {
        return;
    }

    for (const FPCGNamedOverrideGraph& Entry : Entries)
    {
        if (!Entry.IsValid() || !Entry.Override.IsActive())
        {
            continue;
        }

        const FString Prefix = Entry.ContractName.ToString();
        const FName UseGraphAttrName = FName(Prefix + TEXT(".bUseGraph"));
        const FName GraphAttrName = FName(Prefix + TEXT(".Graph"));

        if (FPCGMetadataAttribute<bool>* Attr =
            DataDomain->FindOrCreateAttribute<bool>(UseGraphAttrName, false,
                /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
                /*bOverwriteIfTypeMismatch=*/false))
        {
            Attr->SetValue(PCGInvalidEntryKey, Entry.Override.bUseGraph);
        }

        if (FPCGMetadataAttribute<FSoftObjectPath>* Attr =
            DataDomain->FindOrCreateAttribute<FSoftObjectPath>(GraphAttrName, FSoftObjectPath(),
                /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
                /*bOverwriteIfTypeMismatch=*/false))
        {
            Attr->SetValue(PCGInvalidEntryKey,Entry.Override.Graph.Get());
        }
    }
}