#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "OverrideGraphs.h"
#include "PCGSplineComponent.generated.h"

class UPCGGraphInterface;

UCLASS(ClassGroup = "PCG", meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class PCGUTILS_API UPCGSplineComponent : public USplineComponent,
                                          public IPCGOverrideGraphProvider
{
    GENERATED_BODY()

public:
    UPCGSplineComponent();

    // IPCGOverrideGraphProvider
    virtual TArray<FPCGNamedOverrideGraph> GetOverrideGraphEntries_Implementation() const override;

	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Spline")
    void OnUpdatedSpline();
    void OnUpdatedSpline_Implementation() { }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    float Height = 0.0f;

    // -------------------------------------------------------------------------
// PCG|Bake overrides
// -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PreBakeGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PostBakeGraph;

    // -------------------------------------------------------------------------
    // PCG|Spline overrides
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PreProcessSplineGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PostSpawnGraph;
    
    
    // -------------------------------------------------------------------------
    // PCG|Bake overrides
    // -------------------------------------------------------------------------

   //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (InlineEditConditionToggle))
   //bool bUsePreBakeGraph = false;
   //
   //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (EditCondition = "bUsePreBakeGraph", Tooltip = "Executes directly before baking. Output data will be baked."))
   //TObjectPtr<UPCGGraphInterface> PreBakeGraph;

/*    // -------------------------------------------------------------------------
    // PCG|Spline overrides
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides",meta = (InlineEditConditionToggle))
    bool bUsePreProcessSplineGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (EditCondition = "bUsePreProcessSplineGraph", Tooltip = "Executes before spline data is processed. Allows modification of spline input data."))
    TObjectPtr<UPCGGraphInterface> PreProcessSplineGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", AdvancedDisplay, meta = (InlineEditConditionToggle))
    bool bUseSplineProcessorStack = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", AdvancedDisplay, meta = (EditCondition = "bUseSplineProcessorStack"))
    TArray<FPCGOverrideGraph> SplineProcessorStack;
*/

};