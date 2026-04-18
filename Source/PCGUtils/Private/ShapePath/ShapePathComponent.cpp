#include "ShapePath/ShapePathComponent.h"
#include "PCGActorBase.h"
UShapePathComponent::UShapePathComponent()
{
	Generator = CreateDefaultSubobject<UCircleGenerator>(TEXT("DefaultGenerator"));
}

void UShapePathComponent::RebuildPoints()
{
	CachedPoints.Reset();
	if (Generator)
	{
		Generator->GeneratePoints(CachedPoints);
	}
}

void UShapePathComponent::OnRegister()
{
	Super::OnRegister();
	RebuildPoints();
}

const TArray<FVector>& UShapePathComponent::GetPathPoints()
{
	if (CachedPoints.IsEmpty())
	{
		RebuildPoints();
	}
	return CachedPoints;
}

int32 UShapePathComponent::GetNumPoints() const
{
	return CachedPoints.Num();
}

bool UShapePathComponent::GetIsClosedLoop() const
{
	return Generator ? Generator->IsClosedLoop() : false;
}

FTransform UShapePathComponent::GetPathTransform() const
{
	return GetComponentTransform();
}

FVector UShapePathComponent::GetPathPoint(int32 PointIndex) const
{
	if (!CachedPoints.IsValidIndex(PointIndex))
	{
		return  FVector(0.0f, 0.0f, 0.0f);
	}
	return CachedPoints[PointIndex];
}

#if WITH_EDITOR
void UShapePathComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	RebuildPoints();
	MarkRenderStateDirty();
	if (APCGActorBase* actor = Cast<APCGActorBase>(GetOwner()))
	{
		actor->TriggerRegeneratePCGOnComponentEdits(this);
	}
}

void UShapePathComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (APCGActorBase* actor = Cast<APCGActorBase>(GetOwner()))
	{
		actor->TriggerRegeneratePCGOnComponentEdits(this);
	}
}
#endif
