#include "ShapePath/ShapePathComponent.h"
#include "PCGUtilsHelpers.h"
UShapePathComponent::UShapePathComponent()
{
	Generator = CreateDefaultSubobject<UCircleGenerator>(TEXT("DefaultGenerator"));
}

void UShapePathComponent::PostLoad()
{
	Super::PostLoad();
	if (!bPathDataMigrated)
	{
		PathData.ProcessPathGraph = PreProcessShapePath;
		PathData.Height = PathHeight;
		PathData.GroupID = GroupID;
		PathData.bSetPathDensity = bSetPathDensity;
		PathData.PathDensity = PathDensity;
		PathData.bSetPathColor = bSetPathColor;
		PathData.PathColor = PathColor;
		bPathDataMigrated = true;
	}
}

void UShapePathComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	bPathDataMigrated = true;
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

const TArray<FVector>& UShapePathComponent::GetGeneratedPathPoints()
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

bool UShapePathComponent::IsClosedLoop() const
{
	return Generator ? Generator->IsClosedLoop() : false;
}

TArray<FPCGPoint> UShapePathComponent::GetPathPoints_Implementation() const
{
	UShapePathComponent* MutableThis = const_cast<UShapePathComponent*>(this);
	const TArray<FVector>& LocalPoints = MutableThis->GetGeneratedPathPoints();
	TArray<FPCGPoint> Result;
	Result.Reserve(LocalPoints.Num());
	for (const FVector& LocalPosition : LocalPoints)
	{
		FPCGPoint& Point = Result.Emplace_GetRef();
		Point.Transform = FTransform(LocalPosition);
		Point.SetExtents(FVector(50.f));
		Point.Density = PathData.GetPathDensity();
		Point.Color = PathData.GetPathColor();
	}
	return Result;
}

bool UShapePathComponent::GetIsClosedLoop_Implementation() const
{
	return IsClosedLoop();
}

bool UShapePathComponent::GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const
{
	OutBounds.Init();
	if (!IsValid(Actor))
	{
		return false;
	}

	UShapePathComponent* MutableThis = const_cast<UShapePathComponent*>(this);
	const TArray<FVector>& LocalPoints = MutableThis->GetGeneratedPathPoints();
	const FTransform ComponentTransform = GetComponentTransform();
	const FTransform ActorTransform = Actor->GetActorTransform();
	for (const FVector& LocalPoint : LocalPoints)
	{
		const FVector WorldPoint = ComponentTransform.TransformPosition(LocalPoint);
		OutBounds += ActorTransform.InverseTransformPosition(WorldPoint);
	}

	if (OutBounds.IsValid)
	{
		OutBounds = OutBounds.ExpandBy(1.0f);
	}
	return OutBounds.IsValid != 0;
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
	if (bAllowRegeneratePCGOnEdits)
	{
		UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
	}
}

void UShapePathComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		if (bAllowRegeneratePCGOnEdits)
		{
			UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
		}
	}
}

void UShapePathComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (bAllowRegeneratePCGOnEdits)
	{
		UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
	}
}
#endif
