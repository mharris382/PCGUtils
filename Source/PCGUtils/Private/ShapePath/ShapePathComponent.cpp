#include "ShapePath/ShapePathComponent.h"

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

#if WITH_EDITOR
void UShapePathComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	RebuildPoints();
	MarkRenderStateDirty();
}
#endif
