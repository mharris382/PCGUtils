// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PCGShapeSplineComponent.h"

#include "ShapePath/ShapePathGenerator.h"


// Sets default values for this component's properties
UPCGShapeSplineComponent::UPCGShapeSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Generator = CreateDefaultSubobject<UCircleGenerator>(TEXT("DefaultGenerator"));
}

void UPCGShapeSplineComponent::PostLoad()
{
	Super::PostLoad();
	if (!bGeneratedOnce)
	{
		RebuildPoints();
	}
	else
	{
		SetAllPointTypesLinear();
		UpdateSpline();
	}
}

void UPCGShapeSplineComponent::OnRegister()
{
	Super::OnRegister();
	if (!bGeneratedOnce)
	{
		RebuildPoints();
	}
}

void UPCGShapeSplineComponent::RebuildPoints()
{
	if (bIsRebuilding)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bIsRebuilding, true);
	TArray<FVector> GeneratedPoints;
	if (Generator)
	{
		Generator->GeneratePoints(GeneratedPoints);
	}

	const bool bNewClosedLoop = Generator ? Generator->IsClosedLoop() : false;
	const TSubclassOf<UShapePathGenerator> NewGeneratorClass = Generator ? Generator->GetClass() : nullptr;
	const bool bTopologyChanged = !bGeneratedOnce
		|| GeneratedPoints.Num() != CachedPoints.Num()
		|| GetNumberOfSplinePoints() != GeneratedPoints.Num()
		|| NewGeneratorClass != CachedGeneratorClass
		|| bNewClosedLoop != bCachedClosedLoop;

#if WITH_EDITORONLY_DATA
	if (!bAllowManualPointEdits || bTopologyChanged
		|| EditedPointsDeltaPositions.Num() != GeneratedPoints.Num())
	{
		EditedPointsDeltaPositions.Init(FVector::ZeroVector, GeneratedPoints.Num());
		SetSplinePoints(GeneratedPoints, ESplineCoordinateSpace::Local, false);
	}
	else
	{
		TArray<FVector> FinalPoints = GeneratedPoints;
		for (int32 PointIndex = 0; PointIndex < GeneratedPoints.Num(); ++PointIndex)
		{
			const FVector Delta = EditedPointsDeltaPositions[PointIndex];
			if (Delta.IsNearlyZero())
			{
				EditedPointsDeltaPositions[PointIndex] = FVector::ZeroVector;
				continue;
			}

			if (bPositionEditsAreRelativeToShape)
			{
				FinalPoints[PointIndex] = GeneratedPoints[PointIndex] + Delta;
			}
			else
			{
				const FVector LockedPosition = CachedPoints[PointIndex] + Delta;
				FinalPoints[PointIndex] = LockedPosition;
				EditedPointsDeltaPositions[PointIndex] = LockedPosition - GeneratedPoints[PointIndex];
			}
		}
		SetSplinePoints(FinalPoints, ESplineCoordinateSpace::Local, false);
	}
#else
	SetSplinePoints(GeneratedPoints, ESplineCoordinateSpace::Local, false);
#endif

	SetClosedLoop(bNewClosedLoop, false);
	SetAllPointTypesLinear();
	UpdateSpline();
	CachedPoints = MoveTemp(GeneratedPoints);
	CachedGeneratorClass = NewGeneratorClass;
	bCachedClosedLoop = bNewClosedLoop;
	bGeneratedOnce = true;
}

void UPCGShapeSplineComponent::SetAllPointTypesLinear()
{
	for (int32 PointIndex = 0; PointIndex < GetNumberOfSplinePoints(); ++PointIndex)
	{
		SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
}

#if WITH_EDITOR

void UPCGShapeSplineComponent::ResetEditedPositions()
{
	EditedPointsDeltaPositions.Init(FVector::ZeroVector, CachedPoints.Num());
	RebuildPoints();
}

void UPCGShapeSplineComponent::CaptureManualPositionEdits()
{
	if (bIsRebuilding || !bAllowManualPointEdits || !bGeneratedOnce
		|| GetNumberOfSplinePoints() != CachedPoints.Num())
	{
		return;
	}

	EditedPointsDeltaPositions.SetNumZeroed(CachedPoints.Num());
	for (int32 PointIndex = 0; PointIndex < CachedPoints.Num(); ++PointIndex)
	{
		const FVector CurrentPosition = GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local);
		const FVector Delta = CurrentPosition - CachedPoints[PointIndex];
		EditedPointsDeltaPositions[PointIndex] = Delta.IsNearlyZero() ? FVector::ZeroVector : Delta;
	}
}

void UPCGShapeSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	CaptureManualPositionEdits();
	RebuildPoints();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGShapeSplineComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	CaptureManualPositionEdits();
	RebuildPoints();
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif


