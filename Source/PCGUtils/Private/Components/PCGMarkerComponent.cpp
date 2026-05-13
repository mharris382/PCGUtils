// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PCGMarkerComponent.h"


// Sets default values for this component's properties
UPCGMarkerComponent::UPCGMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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
}


#endif

void UPCGMarkerComponent::GetEditorColors_Implementation(bool& hasScriptedEditorColors, FLinearColor& UnselectedColor,
	FLinearColor& SelectedColor) const
{
	hasScriptedEditorColors = false;
}



