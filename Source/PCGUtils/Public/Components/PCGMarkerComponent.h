// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OverrideGraphs.h"
#include "Components/SceneComponent.h"
#include "PCGMarkerComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UPCGMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UPCGMarkerComponent(const FObjectInitializer& ObjectInitializer);
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker")
	FVector BoundsMax = FVector(50.0f, 50.0f, 50.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker")
	FVector BoundsMin = FVector(-50.0f, -50.0f, -50.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker")
	int Priority = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker", meta = (InlineEditConditionToggle))
	bool bSetDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker", meta = (EditCondition="bSetDensity", UIMin=0, UIMax=1))
	float Density = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker", meta = (InlineEditConditionToggle))
	bool bSetPointColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker", meta = (EditCondition="bSetPointColor"))
	FLinearColor PointColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker", AdvancedDisplay)
	FPCGOverrideGraph PointOverrideGraph;
	
#if WITH_EDITORONLY_DATA
	
	UPROPERTY(EditAnywhere, Category = Editor, meta = (DisplayName="Editor Selected Color"))
	FLinearColor EditorSelectedMarkerColor = FLinearColor::White;
	
	UPROPERTY(EditAnywhere, Category = Editor, meta = (DisplayName="Editor Unselected Color"))
	FLinearColor EditorUnselectedMarkerColor = FLinearColor::Gray;
	
	UPROPERTY(EditAnywhere, Category = Editor, meta = (DisplayName="Fill Opacity", UIMin=0, UIMax=1))
	float EditorFillOpacity = .1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= Editor, meta = (DisplayName="Use Point Color As Fill Color", ToolTip="If bSetPointColor=true then the editor fill color will be the same as the assigned point color"))
	bool bUsePointColorAsEditorColor = true;
	
#endif

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UFUNCTION(BlueprintNativeEvent, Category="Editor")
	void GetEditorColors(bool& hasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor) const;
	virtual void GetEditorColors_Implementation(bool& hasScriptedEditorColors,  FLinearColor& UnselectedColor, FLinearColor& SelectedColor) const;

};
