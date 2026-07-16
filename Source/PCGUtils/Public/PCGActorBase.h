#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/PCGBakeSettings.h"
#include "Interfaces/PCGBakeSettingsProvider.h"
#include "Interfaces/PCGComponentProvider.h"
#include "OverrideGraphs.h"
#include "PCGActorBase.generated.h"

class UActorComponent;
class UBoxComponent;
class UPCGComponent;
class UPCGGenDataAsset;
class UPCGGraphInterface;

UCLASS(Blueprintable, meta = (DisplayName = "PCGActor"))
class PCGUTILS_API APCGActorBase : public AActor, public IPCGBakeSettingsProvider, public IPCGComponentProvider
{
    GENERATED_BODY()

public:
    APCGActorBase();
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual FPCGUtilsBakeSettings GetPCGBakeSettings_Implementation() const override { return BakeSettings; }
	virtual UPCGComponent* GetPrimaryPCGComponent_Implementation() const override { return PCGComponent; }
	virtual bool AllowsComponentTriggeredRegeneration_Implementation() const override { return bAllowComponentEditsToTriggerGeneration; }

    /**
     * Moves the actor's world pivot to the center of its computed bounding box
     * without changing the world-space positions of any attached spline components.
     * Calls OnLocalSpaceDataRemapped so Blueprint subclasses can remap additional
     * local-space data (e.g. vectors with ShowAsWidgets) to preserve their world positions.
     */
    UFUNCTION(CallInEditor, Category = "PCG|Utilities")
    void RecenterActorToBounds();

    virtual void TriggerRegeneratePCGOnComponentEdits(UActorComponent* TriggeringComponent = nullptr);
    
    virtual void TriggerRegenerateOnActorEdits(AActor* OtherActor);
    
protected:

    virtual void OnConstruction(const FTransform& Transform) override;

public:
    // Override in child classes to return local-space AABB.
    // Default returns a unit box centered at origin.
    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bounds")
    void ComputeLocalBounds(FVector& OutMin, FVector& OutMax) const;
    virtual void ComputeLocalBounds_Implementation(FVector& OutMin, FVector& OutMax) const;
    
protected:

    UFUNCTION(BlueprintNativeEvent, Category = "PCG|Bake")
	FString GetAssetSaveGroupName() const;
	FString GetAssetSaveGroupName_Implementation() const { return BakeSettings.BakedAssetGroupLabel; }

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake", meta = (ShowOnlyInnerProperties))
	FPCGUtilsBakeSettings BakeSettings;

    // Symmetric padding added to each side of the computed bounds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bounds", meta = (ClampMin = "0.0"))
    float BoundsPadding = 50.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG|Bake|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use BakeSettings.BakedAssetSaveName."))
    FString BakedAssetSaveName;

    /** Output path for baked static mesh and data assets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use BakeSettings.BakedAssetSavePath."))
    FDirectoryPath BakedAssetSavePath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG|Bake|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use BakeSettings.BakedAssetGroupLabel."))
    FString BakedAssetGroupLabel = TEXT("DefaultGroup");
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use BakeSettings.PreBakeGraph."))
    FPCGOverrideGraph PreBakeGraph;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Bake|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use BakeSettings.PostBakeGraph."))
    FPCGOverrideGraph PostBakeGraph;

private:
	UPROPERTY()
	bool bBakeSettingsMigrated = false;

public:

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG", AdvancedDisplay)
    TObjectPtr<UBoxComponent> BoundsBox;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PCG")
    TObjectPtr<UPCGComponent> PCGComponent;

    
    
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Editor" ,AdvancedDisplay)
    bool bAllowComponentEditsToTriggerGeneration = true;

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Editor" ,AdvancedDisplay, meta = (EditCondition="bAllowComponentEditsToTriggerGeneration"))
    //bool bAllowSplineEditsToForceGenerate = true;
#endif
};
