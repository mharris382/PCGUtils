#pragma once

#include "PCGSettings.h"
#include "OverrideGraphs.h"

#include "PCGResolveOverrideGraphs.generated.h"

UENUM(BlueprintType)
enum class EPCGOverrideFallbackSourceMode : uint8
{
    DataAsset,
    DataTable
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGResolveOverrideGraphsSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    // Comma-separated list of @Data attribute names to check on incoming data.
    // Each attribute is expected to be an FSoftObjectPath to a UObject implementing
    // IPCGOverrideGraphProvider. Sources are resolved left-to-right, highest priority first.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString SourceAttributeNames = "ComponentReference,ActorReference";

    // Comma-separated list of contract names to extract. Empty = all contracts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    FString RequestedContracts;

    // Suppress warnings when a named source attribute is not found on incoming data.
    // Enable when feeding arbitrary data that may only have some sources.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    bool bSuppressMissingSourceWarning = false;

    // Whether to use a fallback source and which kind.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback")
    bool bUseFallback = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback",
        meta = (EditCondition = "bUseFallback"))
    EPCGOverrideFallbackSourceMode FallbackMode = EPCGOverrideFallbackSourceMode::DataAsset;

    // Used when FallbackMode == DataAsset. The asset must implement IPCGOverrideGraphProvider
    // either in C++ or Blueprint.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback",
        meta = (EditCondition = "bUseFallback && FallbackMode == EPCGOverrideFallbackSourceMode::DataAsset"))
    TObjectPtr<UDataAsset> FallbackDataAsset;

    // Used when FallbackMode == DataTable. Row name = contract name, row value = FPCGOverrideGraph.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback",
        meta = (EditCondition = "bUseFallback && FallbackMode == EPCGOverrideFallbackSourceMode::DataTable"))
    TObjectPtr<UDataTable> FallbackDataTable;

#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override
    {
        return FName(TEXT("ResolveOverrideGraphs"));
    }
    virtual FText GetDefaultNodeTitle() const override
    {
        return NSLOCTEXT("PCGResolveOverrideGraphs", "NodeTitle", "Resolve Override Graphs");
    }
    virtual FText GetNodeTooltipText() const override;
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILS_API FPCGResolveOverrideGraphsElement : public IPCGElement
{
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
    // Resolves a DataTable into named override entries using row name as contract name.
    static TArray<FPCGNamedOverrideGraph> GetEntriesFromDataTable(
        const UDataTable* DataTable,
        const TArray<FName>& RequestedContracts);

    // Reads a FSoftObjectPath attribute from @Data on the given metadata,
    // loads the object, and returns it if it implements IPCGOverrideGraphProvider.
    static UObject* ResolveProviderFromMetadata(
        const UPCGMetadata* Metadata,
        FName AttributeName);
};