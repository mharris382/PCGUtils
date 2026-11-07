// Copyright Max Harris

#include "Components/PCGVolumeComponent.h"

#include "Components/BrushComponent.h"
#include "GameFramework/Volume.h"
#include "PCGUtilsHelpers.h"

UPCGVolumeComponent::UPCGVolumeComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	bHiddenInGame = true;
	CastShadow = false;
}

AVolume* UPCGVolumeComponent::GetVolume() const
{
	return Cast<AVolume>(GetOwner());
}

FPCGVolumeShapeDesc UPCGVolumeComponent::GetShapeDesc() const
{
	FPCGVolumeShapeDesc Desc;
	Desc.Shape = Shape;
	Desc.BoxExtent = BoxExtent;
	Desc.Radius = Radius;
	Desc.HalfHeight = HalfHeight;
	Desc.Sides = Sides;
	Desc.ShapeToWorld = GetComponentTransform();
	return Desc;
}

bool UPCGVolumeComponent::EncompassesPoint(const FVector& WorldPosition) const
{
	if (Shape == EPCGVolumeShape::OwnerVolume)
	{
		const AVolume* Volume = GetVolume();
		return Volume && Volume->EncompassesPoint(WorldPosition);
	}
	return GetShapeDesc().Contains(WorldPosition);
}

FBoxSphereBounds UPCGVolumeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Owner Volume's extent is the brush's, which the Volume actor already reports.
	const FBox Local = Shape == EPCGVolumeShape::OwnerVolume ? FBox(ForceInit) : GetShapeDesc().GetLocalBounds();
	if (!Local.IsValid)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(Local).TransformBy(LocalToWorld);
}

void UPCGVolumeComponent::PostLoad()
{
	Super::PostLoad();
	if (!bShapeMigrated)
	{
		// Before shapes, the component only ever read its owning Volume actor's brush.
		if (Cast<AVolume>(GetOwner()))
		{
			Shape = EPCGVolumeShape::OwnerVolume;
		}
		bShapeMigrated = true;
	}
}

void UPCGVolumeComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	bShapeMigrated = true;
}

#if WITH_EDITOR
void UPCGVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateBounds();
	UPCGUtilsHelpers::RefreshOwnerPCGBounds(this);
	TriggerRegeneratePCGOnVolumeEdits();
}

void UPCGVolumeComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	UPCGUtilsHelpers::RefreshOwnerPCGBounds(this);
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
