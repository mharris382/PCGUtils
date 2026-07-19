// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PCGSplineComponent.h"
#include "PCGShapeSplineComponent.generated.h"

class UShapePathGenerator;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class PCGUTILS_API UPCGShapeSplineComponent : public UPCGSplineComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UPCGShapeSplineComponent(const FObjectInitializer& ObjectInitializer);
	
	virtual void PostLoad() override;
	virtual void OnRegister() override;

	UPROPERTY(EditAnywhere, Instanced, Category="PCG",meta  = (DisplayPriority=0))
	TObjectPtr<UShapePathGenerator> Generator;
	
#if WITH_EDITORONLY_DATA
	
	UPROPERTY(EditAnywhere, Category="PCG|Editor",meta  = (DisplayPriority=1, ToolTip = "Allows user to modify position of generated points on spline. Note: adding or removing points resets all edits."))
	bool bAllowManualPointEdits = false;
	
	UPROPERTY(EditAnywhere, Category="PCG|Editor",meta  = (DisplayPriority=2, EditCondition = "bAllowManualPointEdits", EditConditionHides, ToolTip = "If true, edited offsets are reapplied relative to regenerated shape positions. Otherwise edited positions remain fixed."))
	bool bPositionEditsAreRelativeToShape = false;
private:
	UPROPERTY()
	TArray<FVector> EditedPointsDeltaPositions;
#endif
	
public:
	void RebuildPoints();
	

private:

	//if allow manual point edits is allowed, when the user makes a change to the spline. 
	//We need to detect that, and store how far away from the generated position that point is.
	//That delta should then be preserved onto any changes made to the generator that cause it to regenerate.
	//NOTE: changing the number of points will reset edited points.
	
	
	UPROPERTY()
	TArray<FVector> CachedPoints;
	
	UPROPERTY()
	bool bGeneratedOnce = false;

	UPROPERTY()
	TSubclassOf<UShapePathGenerator> CachedGeneratorClass;

	UPROPERTY()
	bool bCachedClosedLoop = false;

	UPROPERTY(Transient)
	bool bIsRebuilding = false;
	
#if WITH_EDITOR
	void CaptureManualPositionEdits();
	void ResetEditedPositions();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	void SetAllPointTypesLinear();
	
};
