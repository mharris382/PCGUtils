// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PCGMarkerComponent.h"
#include "PCGUtilsHelpers.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif


// Sets default values for this component's properties
UPCGMarkerComponent::UPCGMarkerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UPCGMarkerComponent::GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const
{
	OutBounds.Init();
	if (!IsValid(Actor))
	{
		return false;
	}

	FBox LocalBounds(EForceInit::ForceInit);
	LocalBounds += BoundsMin;
	LocalBounds += BoundsMax;
	const FTransform ComponentToActor = GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform());
	OutBounds = LocalBounds.TransformBy(ComponentToActor);
	return OutBounds.IsValid != 0;
}

void UPCGMarkerComponent::PostLoad()
{
	Super::PostLoad();
	if (!bPointDataMigrated)
	{
		PointData.GroupID = MarkerGroupID;
		PointData.ProcessPointGraph = PointOverrideGraph;
		PointData.bSetPointDensity = bSetDensity;
		PointData.PointDensity = Density;
		PointData.bSetPointColor = bSetPointColor;
		PointData.PointColor = PointColor;
		bPointDataMigrated = true;
	}
}

bool UPCGMarkerComponent::GetPCGPointData_Implementation(
	TArray<FPCGPoint>& OutPoints,
	FPointComponentData& OutPointData) const
{
	OutPoints.Reset(1);
	FPCGPoint& Point = OutPoints.Emplace_GetRef();
	Point.Transform = GetComponentTransform();
	Point.BoundsMin = BoundsMin;
	Point.BoundsMax = BoundsMax;
	Point.Density = PointData.GetPointDensity();
	Point.Color = PointData.GetPointColor();
	OutPointData = PointData;
	return true;
}

void UPCGMarkerComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	bPointDataMigrated = true;
}


#if WITH_EDITOR

void UPCGMarkerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bool hasScriptedEditorColors = false;
	FLinearColor SelectedColor = FLinearColor::White;
	FLinearColor UnselectedColor = FLinearColor::Black;
	GetEditorColors(hasScriptedEditorColors, SelectedColor, UnselectedColor);
	if (hasScriptedEditorColors)
	{
		EditorSelectedMarkerColor = SelectedColor;
		EditorUnselectedMarkerColor = UnselectedColor;
	}

	TriggerRegeneratePCGOnMarkerEdits();
}

void UPCGMarkerComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		TriggerRegeneratePCGOnMarkerEdits();
	}
}

void UPCGMarkerComponent::TriggerRegeneratePCGOnMarkerEdits()
{
	if (!bRegeneratePCGOnMarkerEdits)
	{
		return;
	}

	UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
}

#endif

void UPCGMarkerComponent::GetEditorColors_Implementation(bool& hasScriptedEditorColors, FLinearColor& UnselectedColor,
	FLinearColor& SelectedColor) const
{
	hasScriptedEditorColors = false;
}



