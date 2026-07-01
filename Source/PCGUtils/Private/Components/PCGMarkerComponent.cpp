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

void UPCGMarkerComponent::SetPivotToBottom()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(NSLOCTEXT("PCGUtils", "SetMarkerPivotToBottom", "Set Marker Pivot To Bottom"));
	Modify();

	const FVector OldBoundsMin = BoundsMin;
	const FVector OldBoundsMax = BoundsMax;
	const FVector BoundsSize = OldBoundsMax - OldBoundsMin;
	const FVector NewWorldLocation = GetComponentTransform().TransformPosition(OldBoundsMin);

	BoundsMin = FVector::ZeroVector;
	BoundsMax = BoundsSize;
	SetWorldLocation(NewWorldLocation);

	TriggerRegeneratePCGOnMarkerEdits();
#endif
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



