#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OverrideGraphs.h"
#include "PCGActorBase.generated.h"

class UBoxComponent;
class UPCGComponent;
class UPCGGenDataAsset;
class UPCGGraphInterface;

UCLASS(Abstract, Blueprintable)
class PCGUTILS_API APCGActorBase : public AActor
{
    GENERATED_BODY()

public:
    APCGActorBase();

    /**
     * Moves the actor's world pivot to the center of its computed bounding box
     * without changing the world-space positions of any attached spline components.
     * Calls OnLocalSpaceDataRemapped so Blueprint subclasses can remap additional
     * local-space data (e.g. vectors with ShowAsWidgets) to preserve their world positions.
     */
    UFUNCTION(CallInEditor, Category = "PCG|Utilities")
    void RecenterActorToBounds();

protected:

    virtual void OnConstruction(const FTransform& Transform) override;

    // Override in child classes to return local-space AABB.
    // Default returns a unit box centered at origin.
    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bounds")
    void ComputeLocalBounds(FVector& OutMin, FVector& OutMax) const;
    virtual void ComputeLocalBounds_Implementation(FVector& OutMin, FVector& OutMax) const;


    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bake")
	FString GetAssetSaveGroupName() const;
	FString GetAssetSaveGroupName_Implementation() const { return TEXT("DefaultGroup"); }

    FBox GetPCGBounds() const;

    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Editor")
    FColor GetBoxEditorColor() const;
    FColor GetBoxEditorColor_Implementation() const    {  return FColor::White.WithAlpha(1.f);   }
    /**
     * Called by RecenterActorToBounds before the actor pivot moves.
     * LocalDeltaTransform is the transform that must be applied to any local-space
     * positions/vectors to preserve their world-space locations after the pivot change.
     * For a translation-only recenter this is a pure translation of -LocalBoundsCenter.
     * Override in Blueprint to remap custom local-space properties such as vectors
     * that have ShowAsWidgets enabled.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Utilities")
    void OnLocalSpaceDataRemapped(const FTransform& LocalDeltaTransform);
    virtual void OnLocalSpaceDataRemapped_Implementation(const FTransform& LocalDeltaTransform);

private:
    void ApplyBoundsToBox();

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    int32 Seed = 0;

    // Symmetric padding added to each side of the computed bounds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bounds", meta = (ClampMin = "0.0"))
    float BoundsPadding = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG|Bake")
    FString BakedAssetSaveName;

    /** Output path for baked static mesh and data assets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake", meta = (ContentDir))
    FDirectoryPath BakedAssetSavePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake")
    FPCGOverrideGraph PreBakeGraph;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake")
    FPCGOverrideGraph PostBakeGraph;
    

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG", AdvancedDisplay)
    TObjectPtr<UBoxComponent> BoundsBox;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG")
    TObjectPtr<UPCGComponent> PCGComponent;

    
};