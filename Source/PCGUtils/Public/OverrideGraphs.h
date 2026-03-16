#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Interface.h"
#include "Engine/DataTable.h"
#include "Metadata/PCGMetadataDomain.h"
#include "OverrideGraphs.generated.h"

class UPCGGraphInterface;
class UPCGMetadata;

// -----------------------------------------------------------------------------
// General purpose single override graph + enable flag.
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGOverrideGraph
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bUseGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UPCGGraphInterface> Graph = nullptr;

    bool IsActive() const { return bUseGraph ; }

    static FPCGOverrideGraph Resolve(
        const FPCGOverrideGraph& Higher,
        const FPCGOverrideGraph& Lower)
    {
        return Higher.IsActive() ? Higher : Lower;
    }
};

// -----------------------------------------------------------------------------
// Named override entry — contract name drives attribute prefix in @Data.
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGNamedOverrideGraph
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ContractName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPCGOverrideGraph Override;

    bool IsValid() const { return ContractName != NAME_None; }
};

// -----------------------------------------------------------------------------
// DataTable row type — row name is the contract name, value is the override.
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGOverrideGraphTableRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPCGOverrideGraph Override;
};

// -----------------------------------------------------------------------------
// Interface — any UObject becomes an override provider by implementing this.
// The interface answers: what override graphs do you have, and what are they named?
// Filtering is the library's concern, not the interface's.
// -----------------------------------------------------------------------------

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGOverrideGraphProvider : public UInterface
{
    GENERATED_BODY()
};

class PCGUTILS_API IPCGOverrideGraphProvider
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Overrides")
    TArray<FPCGNamedOverrideGraph> GetOverrideGraphEntries() const;
    virtual TArray<FPCGNamedOverrideGraph> GetOverrideGraphEntries_Implementation() const
    {
        return {};
    }


    // Returns an optional single fallback source — either a DataTable of
    // FPCGOverrideGraphTableRow rows, or another UObject implementing this interface.
    // Resolved at lower priority than GetOverrideGraphEntries().
    // Only one level of fallback is followed to prevent circular references.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Overrides")
    UObject* GetOverrideGraphFallbackSource() const;
    virtual UObject* GetOverrideGraphFallbackSource_Implementation() const
    {
        return nullptr;
    }
};

// -----------------------------------------------------------------------------
// Blueprint function library
// -----------------------------------------------------------------------------

UCLASS()
class PCGUTILS_API UPCGOverrideGraphLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Single struct helpers ─────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCG|Overrides")
    static FPCGOverrideGraph ResolveOverrideGraph(
        const FPCGOverrideGraph& Higher,
        const FPCGOverrideGraph& Lower)
    {
        return FPCGOverrideGraph::Resolve(Higher, Lower);
    }

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCG|Overrides")
    static bool IsOverrideGraphActive(const FPCGOverrideGraph& Override)
    {
        return Override.IsActive();
    }

    // ── Provider interface helpers ────────────────────────────────────────────

    // Returns entries from a provider, optionally filtered to requested contract names.
    // Empty RequestedContracts = return all.
    UFUNCTION(BlueprintCallable, Category = "PCG|Overrides")
    static TArray<FPCGNamedOverrideGraph> GetOverrideEntries(
        TScriptInterface<IPCGOverrideGraphProvider> Provider,
        const TArray<FName>& RequestedContracts);

    // Resolves a UDataTable of FPCGOverrideGraphTableRow rows into named override entries.
    // Row name = contract name.
    UFUNCTION(BlueprintCallable, Category = "PCG|Overrides")
    static TArray<FPCGNamedOverrideGraph> GetOverrideEntriesFromDataTable(
        const UDataTable* DataTable,
        const TArray<FName>& RequestedContracts);

    // Resolves a priority-ordered list of providers into a flat composite list.
    // Index 0 = highest priority. First active entry per contract name wins.
    UFUNCTION(BlueprintCallable, Category = "PCG|Overrides")
    static TArray<FPCGNamedOverrideGraph> ResolveOverrideStack(
        const TArray<TScriptInterface<IPCGOverrideGraphProvider>>& Providers,
        const TArray<FName>& RequestedContracts);

    // Stamps a resolved entry list into PCG @Data metadata.
    static void StampOverrideEntriesToMetadata(
        const TArray<FPCGNamedOverrideGraph>& Entries,
        UPCGMetadata* Metadata);

    static void StampOverrideEntriesToMetadataDomain(const TArray<FPCGNamedOverrideGraph>& Entries,FPCGMetadataDomain* DataDomain);
    // ── C++ convenience — raw UObject*, skips TScriptInterface wrapping ───────

    static TArray<FPCGNamedOverrideGraph> GetOverrideEntriesFromObject(
        UObject* Object,
        const TArray<FName>& RequestedContracts);

    static TArray<FPCGNamedOverrideGraph> ResolveOverrideStackFromObjects(
        const TArray<UObject*>& Providers,
        const TArray<FName>& RequestedContracts);
};