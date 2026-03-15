#pragma once

#include "CoreMinimal.h"
#include "PCGGraph.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OverrideGraphs.generated.h"



// -----------------------------------------------------------------------------
// General purpose single override graph + enable flag.
// The property name on the owning class is the contract identifier —
// PCG graphs read by name and don't care about class hierarchy.
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGOverrideGraph
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle))
    bool bUseGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite,
        meta = (EditCondition = "bUseGraph"))
    TSoftObjectPtr<UPCGGraphInterface> Graph = nullptr;

    bool IsActive() const { return bUseGraph ; }

    static FPCGOverrideGraph Resolve(
        const FPCGOverrideGraph& Higher,
        const FPCGOverrideGraph& Lower)
    {
        return Higher.IsActive() ? Higher : Lower;
    }
};


USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGBakeOverrideGraphs
{
    GENERATED_BODY()


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", meta = (InlineEditConditionToggle))
    bool bUsePreBakeGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", meta = (EditCondition = "bUsePreBakeGraph", Tooltip = "Executes directy before baking occurs.  The output data will be baked"))
    TSoftObjectPtr<UPCGGraphInterface> PreBakeGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", meta = (InlineEditConditionToggle))
    bool bUsePostBakeGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", meta = (EditCondition = "bUsePostBakeGraph", Tooltip = "Executes directy before baking occurs. Input will be a save path"))
    TSoftObjectPtr<UPCGGraphInterface> PostBakeGraph;
};



UCLASS()
class PCGUTILS_API UPCGOverrideGraphLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCG|Overrides")
    static FPCGOverrideGraph ResolveOverrideGraph(
        const FPCGOverrideGraph& Higher,
        const FPCGOverrideGraph& Lower)
    {
        return FPCGOverrideGraph::Resolve(Higher, Lower);
    }
};