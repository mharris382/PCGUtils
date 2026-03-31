// Copyright Max Harris

#include "PCGUtilsHelpers.h"

#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSplineBoundingBox
// ─────────────────────────────────────────────────────────────────────────────

FBox UPCGUtilsHelpers::ComputeSplineBoundingBox(
	const USplineComponent* SplineComponent,
	bool bLocalSpace,
	int32 SampleSubdivisionCount)
{
	if (!SplineComponent)
	{
		return FBox(EForceInit::ForceInit);
	}

	// Clamp to the documented minimum of 0.
	const int32 Subdivisions = FMath::Max(0, SampleSubdivisionCount);

	FBox Result(EForceInit::ForceInit);

	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints == 0)
	{
		return Result;
	}

	// Helper: convert a world-space position to the desired output space.
	// When bLocalSpace is true we want actor-local space, not spline-component-local space,
	// so that the bounding box accounts for the spline component's RelativeTransform offset.
	const AActor* Owner = SplineComponent->GetOwner();
	const FTransform LocalSpaceTransform = Owner ? Owner->GetActorTransform() : SplineComponent->GetComponentTransform();
	auto ToOutputSpace = [&](const FVector& WorldPos) -> FVector
	{
		return bLocalSpace ? LocalSpaceTransform.InverseTransformPosition(WorldPos) : WorldPos;
	};

	// ── Control points ────────────────────────────────────────────────────────
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FVector WorldPos = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
		Result += ToOutputSpace(WorldPos);
	}

	// ── Intra-segment samples ─────────────────────────────────────────────────
	if (Subdivisions > 0)
	{
		const bool bIsClosed   = SplineComponent->IsClosedLoop();
		const int32 NumSegments = bIsClosed ? NumPoints : NumPoints - 1;

		// Step size in [0,1] normalised per-segment space.
		const float StepT = 1.0f / static_cast<float>(Subdivisions + 1);

		for (int32 SegIndex = 0; SegIndex < NumSegments; ++SegIndex)
		{
			// Distance range for this segment.
			const float SegStartDist = SplineComponent->GetDistanceAlongSplineAtSplinePoint(SegIndex);
			const float SegEndDist   = SplineComponent->GetDistanceAlongSplineAtSplinePoint(
				(SegIndex + 1) % NumPoints);

			// Handle wrap-around on closed loops where end < start.
			const float SplineLength  = SplineComponent->GetSplineLength();
			const float EffectiveEnd  = (SegEndDist > SegStartDist)
				? SegEndDist
				: SegEndDist + SplineLength;
			const float SegLength     = EffectiveEnd - SegStartDist;

			for (int32 SubIndex = 1; SubIndex <= Subdivisions; ++SubIndex)
			{
				const float T        = SubIndex * StepT;
				const float Distance = FMath::Fmod(SegStartDist + T * SegLength, SplineLength);
				const FVector WorldPos = SplineComponent->GetLocationAtDistanceAlongSpline(
					Distance, ESplineCoordinateSpace::World);

				Result += ToOutputSpace(WorldPos);
			}
		}
	}

	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeActorSplineBoundingBox
// ─────────────────────────────────────────────────────────────────────────────

FBox UPCGUtilsHelpers::ComputeActorSplineBoundingBox(
	const AActor* Actor,
	bool bLocalSpace,
	int32 SampleSubdivisionCount)
{
	if (!Actor)
	{
		return FBox(EForceInit::ForceInit);
	}

	FBox Result(EForceInit::ForceInit);

	TArray<USplineComponent*> SplineComponents;
	Actor->GetComponents<USplineComponent>(SplineComponents);

	for (const USplineComponent* Spline : SplineComponents)
	{
		const FBox SplineBox = ComputeSplineBoundingBox(Spline, bLocalSpace, SampleSubdivisionCount);
		if (SplineBox.IsValid)
		{
			Result += SplineBox;
		}
	}

	return Result;
}
