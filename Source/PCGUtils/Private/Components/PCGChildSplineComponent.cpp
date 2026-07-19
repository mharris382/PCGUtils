#include "Components/PCGChildSplineComponent.h"

#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	ESplinePointType::Type ToSplinePointType(EPCGChildSplineEndpointTangentMode Mode)
	{
		switch (Mode)
		{
		case EPCGChildSplineEndpointTangentMode::Linear: return ESplinePointType::Linear;
		case EPCGChildSplineEndpointTangentMode::Free: return ESplinePointType::CurveCustomTangent;
		default: return ESplinePointType::Curve;
		}
	}

	uint32 HashVector(const FVector& Value)
	{
		return HashCombine(HashCombine(GetTypeHash(Value.X), GetTypeHash(Value.Y)), GetTypeHash(Value.Z));
	}

	float WrapNormalizedTime(float Value)
	{
		// Preserve the useful endpoint representation of 1.0 while allowing values outside
		// the normal UI range to wrap around a closed source spline.
		if (Value >= 0.0f && Value <= 1.0f)
		{
			return Value;
		}

		float Wrapped = FMath::Fmod(Value, 1.0f);
		if (Wrapped < 0.0f)
		{
			Wrapped += 1.0f;
		}
		return Wrapped;
	}
}

UPCGChildSplineComponent::UPCGChildSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetClosedLoop(false, false);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;
}

USplineComponent* UPCGChildSplineComponent::GetSourceSpline() const
{
	if (bCopyExternalSpline)
	{
		if (!ExternalSplineActor || ExternalSplineComponentTag.IsNone())
		{
			return nullptr;
		}

		TInlineComponentArray<USplineComponent*> SplineComponents(ExternalSplineActor);
		for (USplineComponent* SplineComponent : SplineComponents)
		{
			if (IsValid(SplineComponent)
				&& SplineComponent != this
				&& SplineComponent->ComponentHasTag(ExternalSplineComponentTag))
			{
				return SplineComponent;
			}
		}

		return nullptr;
	}

	return Cast<USplineComponent>(GetAttachParent());
}

void UPCGChildSplineComponent::OnRegister()
{
	Super::OnRegister();
	RefreshSpline();
}

uint32 UPCGChildSplineComponent::CalculateSourceHash() const
{
	const USplineComponent* Source = GetSourceSpline();
	if (!Source)
	{
		return 0;
	}

	uint32 Hash = GetTypeHash(Source->GetNumberOfSplinePoints());
	Hash = HashCombine(Hash, GetTypeHash(Source->IsClosedLoop()));
	Hash = HashCombine(Hash, HashVector(Source->GetComponentLocation()));
	Hash = HashCombine(Hash, HashVector(Source->GetComponentScale()));
	Hash = HashCombine(Hash, HashVector(Source->GetComponentRotation().Euler()));
	for (int32 Index = 0; Index < Source->GetNumberOfSplinePoints(); ++Index)
	{
		const FSplinePoint Point = Source->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
		Hash = HashCombine(Hash, HashVector(Point.Position));
		Hash = HashCombine(Hash, HashVector(Point.ArriveTangent));
		Hash = HashCombine(Hash, HashVector(Point.LeaveTangent));
		Hash = HashCombine(Hash, HashVector(Point.Rotation.Euler()));
		Hash = HashCombine(Hash, HashVector(Point.Scale));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Point.Type)));
	}
	return Hash;
}

void UPCGChildSplineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const uint32 CurrentHash = CalculateSourceHash();
	if (CurrentHash != 0 && CurrentHash != LastSourceHash)
	{
		RefreshSpline();
	}
}

void UPCGChildSplineComponent::RefreshSpline()
{
	if (bRefreshingSpline)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingSpline, true);

	USplineComponent* Source = GetSourceSpline();
	if (!Source || Source == this || Source->GetNumberOfSplinePoints() < 1)
	{
		LastSourceHash = 0;
		return;
	}

	const int32 ExistingCount = GetNumberOfSplinePoints();
	TArray<FSplinePoint> PreservedStart;
	TArray<FSplinePoint> PreservedEnd;
	const int32 OldStartCount = FMath::Min3(PreservedFreeStartPointCount, ExistingCount, FreeStartPointCount);
	const int32 OldEndCount = FMath::Min3(PreservedFreeEndPointCount, ExistingCount - OldStartCount, FreeEndingPointCount);
	for (int32 Index = 0; Index < OldStartCount; ++Index)
	{
		PreservedStart.Add(GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
	}
	for (int32 Index = ExistingCount - OldEndCount; Index < ExistingCount; ++Index)
	{
		PreservedEnd.Add(GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
	}

	const int32 NumSourcePoints = Source->GetNumberOfSplinePoints();
	const bool bSourceClosedLoop = Source->IsClosedLoop();
	const float MaxSourceKey = static_cast<float>(bSourceClosedLoop ? NumSourcePoints : FMath::Max(0, NumSourcePoints - 1));
	float StartTime = bSourceClosedLoop ? WrapNormalizedTime(CopySplineStartTime) : FMath::Clamp(CopySplineStartTime, 0.0f, 1.0f);
	float EndTime = bSourceClosedLoop ? WrapNormalizedTime(CopySplineEndTime) : FMath::Clamp(CopySplineEndTime, 0.0f, 1.0f);
	if (bSourceClosedLoop && EndTime < StartTime)
	{
		EndTime += 1.0f;
	}
	else if (!bSourceClosedLoop)
	{
		EndTime = FMath::Max(EndTime, StartTime);
	}
	const float LowKey = StartTime * MaxSourceKey;
	const float HighKey = EndTime * MaxSourceKey;

	TArray<float> SourceKeys;
	SourceKeys.Add(LowKey);
	for (int32 PointIndex = FMath::CeilToInt(LowKey); PointIndex < HighKey; ++PointIndex)
	{
		if (!FMath::IsNearlyEqual(static_cast<float>(PointIndex), LowKey))
		{
			SourceKeys.Add(static_cast<float>(PointIndex));
		}
	}
	if (!FMath::IsNearlyEqual(HighKey, LowKey))
	{
		SourceKeys.Add(HighKey);
	}
	TArray<FSplinePoint> CopiedPoints;
	CopiedPoints.Reserve(SourceKeys.Num());
	const FTransform ChildWorld = GetComponentTransform();
	for (int32 OutputIndex = 0; OutputIndex < SourceKeys.Num(); ++OutputIndex)
	{
		const float TraversalKey = SourceKeys[OutputIndex];
		// Closed-loop traversal keys are intentionally allowed to exceed MaxSourceKey so
		// point iteration remains monotonically forward across the seam. Spline sampling,
		// however, must restart at key zero after reaching the closing key.
		float SourceKey = TraversalKey;
		if (bSourceClosedLoop && SourceKey > MaxSourceKey)
		{
			SourceKey = FMath::Fmod(SourceKey, MaxSourceKey);
		}

		FTransform WorldTransform = Source->GetTransformAtSplineInputKey(SourceKey, ESplineCoordinateSpace::World, true);
		WorldTransform.AddToTranslation(FVector(0.0, 0.0, ZOffset));
		const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ChildWorld);
		FVector WorldTangent = Source->GetTangentAtSplineInputKey(SourceKey, ESplineCoordinateSpace::World);
		const FVector LocalTangent = ChildWorld.InverseTransformVector(WorldTangent);

		ESplinePointType::Type PointType = ESplinePointType::Curve;
		const bool bIsExactSourcePoint = FMath::IsNearlyEqual(SourceKey, FMath::RoundToFloat(SourceKey));
		if (bIsExactSourcePoint)
		{
			PointType = Source->GetSplinePointType(FMath::RoundToInt(SourceKey) % NumSourcePoints);
		}
		if (OutputIndex == 0)
		{
			PointType = ToSplinePointType(StartTangentMode);
		}
		else if (OutputIndex == SourceKeys.Num() - 1)
		{
			PointType = ToSplinePointType(EndTangentMode);
		}

		CopiedPoints.Emplace(static_cast<float>(OutputIndex), LocalTransform.GetLocation(), LocalTangent, LocalTangent,
			LocalTransform.Rotator(), LocalTransform.GetScale3D(), PointType);
	}

	const FVector StartDirection = CopiedPoints.Num() > 0 ? CopiedPoints[0].LeaveTangent.GetSafeNormal() : FVector::ForwardVector;
	const FVector EndDirection = CopiedPoints.Num() > 0 ? CopiedPoints.Last().ArriveTangent.GetSafeNormal() : FVector::ForwardVector;
	while (PreservedStart.Num() < FreeStartPointCount)
	{
		const int32 MissingIndex = FreeStartPointCount - PreservedStart.Num();
		const FVector Position = CopiedPoints[0].Position - StartDirection * (100.0f * MissingIndex);
		PreservedStart.Insert(FSplinePoint(0.0f, Position, ESplinePointType::Curve), 0);
	}
	while (PreservedEnd.Num() < FreeEndingPointCount)
	{
		const int32 AddedIndex = PreservedEnd.Num() + 1;
		const FVector Position = CopiedPoints.Last().Position + EndDirection * (100.0f * AddedIndex);
		PreservedEnd.Add(FSplinePoint(0.0f, Position, ESplinePointType::Curve));
	}

	TArray<FSplinePoint> NewPoints;
	NewPoints.Append(PreservedStart);
	NewPoints.Append(CopiedPoints);
	NewPoints.Append(PreservedEnd);
	for (int32 Index = 0; Index < NewPoints.Num(); ++Index)
	{
		NewPoints[Index].InputKey = static_cast<float>(Index);
	}

	ClearSplinePoints(false);
	AddPoints(NewPoints, false);
	SetClosedLoop(false, false);
	UpdateSpline();
	GeneratedCopiedPointCount = CopiedPoints.Num();
	PreservedFreeStartPointCount = FreeStartPointCount;
	PreservedFreeEndPointCount = FreeEndingPointCount;
	LastSourceHash = CalculateSourceHash();
	OnUpdatedSpline();
}

#if WITH_EDITOR
void UPCGChildSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (const USplineComponent* Source = GetSourceSpline(); Source && !Source->IsClosedLoop())
	{
		CopySplineStartTime = FMath::Clamp(CopySplineStartTime, 0.0f, 1.0f);
		CopySplineEndTime = FMath::Clamp(CopySplineEndTime, CopySplineStartTime, 1.0f);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!bRefreshingSpline)
	{
		RefreshSpline();
	}
}
#endif
