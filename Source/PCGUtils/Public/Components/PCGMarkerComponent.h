// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "Interfaces/PCGBoundsProvider.h"
#include "Interfaces/PCGPointProvider.h"
#include "OverrideGraphs.h"
#include "Components/SceneComponent.h"
#include "PCGMarkerComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UPCGMarkerComponent : public USceneComponent, public IPCGBoundsProvider, public IPCGPointProvider
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UPCGMarkerComponent(const FObjectInitializer& ObjectInitializer);
	virtual bool GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const override;
	virtual bool GetPCGPointData_Implementation(TArray<FPCGPoint>& OutPoints, FPointComponentData& OutPointData) const override;

	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker")
	FVector BoundsMax = FVector(50.0f, 50.0f, 50.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker")
	FVector BoundsMin = FVector(-50.0f, -50.0f, -50.0f);

	/** General-purpose data used to construct the marker's output point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ShowOnlyInnerProperties))
	FPointComponentData PointData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta=(DeprecatedProperty, DeprecationMessage="Use PointData.GroupID instead."))
	int32 MarkerGroupID = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta = (DeprecatedProperty, DeprecationMessage="Use PointData.ProcessPointGraph instead."))
	FPCGOverrideGraph PointOverrideGraph;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta = (DeprecatedProperty, DeprecationMessage="Use PointData.bSetPointDensity instead."))
	bool bSetDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta = (DeprecatedProperty, DeprecationMessage="Use PointData.PointDensity instead."))
	float Density = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta = (DeprecatedProperty, DeprecationMessage="Use PointData.bSetPointColor instead."))
	bool bSetPointColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Marker|Legacy", meta = (DeprecatedProperty, DeprecationMessage="Use PointData.PointColor instead."))
	FLinearColor PointColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
	
	
	
#if WITH_EDITORONLY_DATA
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker|Editor")
	bool bRegeneratePCGOnMarkerEdits = true;
	
	UPROPERTY(EditAnywhere, Category =  "Marker|Editor", meta = (DisplayName="Editor Selected Color"))
	FLinearColor EditorSelectedMarkerColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category = "Marker|Editor")
	bool bDrawSelectedAsWireframe = false;	
	
	UPROPERTY(EditAnywhere, Category =  "Marker|Editor", meta = (DisplayName="Editor Unselected Color"))
	FLinearColor EditorUnselectedMarkerColor = FLinearColor::Gray;
	
	UPROPERTY(EditAnywhere, Category = "Marker|Editor")
	bool bDrawUnselectedAsWireframe = true;	
	
	UPROPERTY(EditAnywhere, Category =  "Marker|Editor", meta = (DisplayName="Fill Opacity", UIMin=0, UIMax=1))
	float EditorFillOpacity = .1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=  "Marker|Editor", meta = (DisplayName="Use Point Color As Fill Color", ToolTip="When Point Data has Set Point Color enabled, use that color for the editor fill."))
	bool bUsePointColorAsEditorColor = true;
	
#endif

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
#endif

protected:
	UFUNCTION(BlueprintNativeEvent, Category="Editor")
	void GetEditorColors(bool& hasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor) const;
	virtual void GetEditorColors_Implementation(bool& hasScriptedEditorColors,  FLinearColor& UnselectedColor, FLinearColor& SelectedColor) const;

#if WITH_EDITOR
	void TriggerRegeneratePCGOnMarkerEdits();
#endif

private:
	UPROPERTY()
	bool bPointDataMigrated = false;

};
