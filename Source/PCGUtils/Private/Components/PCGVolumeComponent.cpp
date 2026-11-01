// Copyright Max Harris

#include "Components/PCGVolumeComponent.h"

#include "GameFramework/Volume.h"
#include "PCGUtilsHelpers.h"

UPCGVolumeComponent::UPCGVolumeComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

AVolume* UPCGVolumeComponent::GetVolume() const
{
	return Cast<AVolume>(GetOwner());
}

#if WITH_EDITOR
void UPCGVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	TriggerRegeneratePCGOnVolumeEdits();
}

void UPCGVolumeComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		TriggerRegeneratePCGOnVolumeEdits();
	}
}

void UPCGVolumeComponent::TriggerRegeneratePCGOnVolumeEdits()
{
	if (bRegeneratePCGOnVolumeEdits)
	{
		UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
	}
}
#endif
