#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGActorBase.generated.h"

class UBoxComponent;
class UPCGComponent;
class UPCGGraphInterface;

UCLASS(Abstract, Blueprintable)
class PCGUTILS_API APCGActorBase : public AActor
{
    GENERATED_BODY()

public:
    APCGActorBase();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;

    // Override in child classes to return local-space AABB.
    // Default returns a unit box centered at origin.
    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bounds")
    void ComputeLocalBounds(FVector& OutMin, FVector& OutMax) const;
    virtual void ComputeLocalBounds_Implementation(FVector& OutMin, FVector& OutMax) const;


    UFUNCTION(BlueprintNativeEvent, Category = "PCG|OutputData")
    const TArray<UObject*> ResolveAllPCGOutputDataConsumers() const;
    const TArray<UObject*> ResolveAllPCGOutputDataConsumers_Implementation() const;


    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bake")
	FString GetAssetSaveGroupName() const;
	FString GetAssetSaveGroupName_Implementation() const { return TEXT("DefaultGroup"); }


private:
    void ApplyBoundsToBox();

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    int32 Seed = 0;

    // Symmetric padding added to each side of the computed bounds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bounds", meta = (ClampMin = "0.0"))
    float BoundsPadding = 0.f;


    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG|Bake", AdvancedDisplay)
    FString BakedAssetSaveName;

    /** Output path for baked static mesh and data assets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake", AdvancedDisplay, meta = (ContentDir))
    FDirectoryPath BakedAssetSavePath;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", AdvancedDisplay, meta = (InlineEditConditionToggle))
	bool bUsePreBakeGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", AdvancedDisplay, meta = (EditCondition = "bUsePreBakeGraph", Tooltip = "Executes directy before baking occurs.  The output data will be baked"))
    TObjectPtr<UPCGGraphInterface> PreBakeGraph;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", AdvancedDisplay, meta = (InlineEditConditionToggle))
    bool bUsePostBakeGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", AdvancedDisplay, meta = (EditCondition = "bUsePostBakeGraph", Tooltip = "Executes directy before baking occurs. Input will be a save path"))
    TObjectPtr<UPCGGraphInterface> PostBakeGraph;


    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG", AdvancedDisplay)
    TObjectPtr<UBoxComponent> BoundsBox;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG", AdvancedDisplay)
    TObjectPtr<UPCGComponent> PCGComponent;

private:

    void EmitPCGOutputDataToConsumers(bool isConstructor);
};