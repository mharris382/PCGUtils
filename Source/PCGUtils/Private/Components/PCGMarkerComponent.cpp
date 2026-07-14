// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PCGMarkerComponent.h"
#include "PCGActorBase.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif


// Sets default values for this component's properties
UPCGMarkerComponent::UPCGMarkerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPCGMarkerComponent::CopyLegacyPointData()
{
	Modify();
	PointData.GroupID = MarkerGroupID;
	PointData.ProcessPointGraph = PointOverrideGraph;
	PointData.bSetPointDensity = bSetDensity;
	PointData.PointDensity = Density;
	PointData.bSetPointColor = bSetPointColor;
	PointData.PointColor = PointColor;
	MarkPackageDirty();
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

	APCGActorBase* Actor = Cast<APCGActorBase>(GetOwner());
	if (!Actor)
	{
		return;
	}

	Actor->TriggerRegeneratePCGOnComponentEdits(this);
}

#endif

void UPCGMarkerComponent::GetEditorColors_Implementation(bool& hasScriptedEditorColors, FLinearColor& UnselectedColor,
	FLinearColor& SelectedColor) const
{
	hasScriptedEditorColors = false;
}



