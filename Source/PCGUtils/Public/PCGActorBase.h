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