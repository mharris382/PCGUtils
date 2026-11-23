// Copyright Max Harris

#include "Points/PCGUtilsElementPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"

#include <atomic>

namespace PCGUtilsElementPointsPrivate
{
	std::atomic<int32> TotalPointDataBuilds{0};
}

// Out of line so TStrongObjectPtr<UPCGBasePointData> only needs the full class here.
FPCGUtilsElementPoints::FPCGUtilsElementPoints() = default;
FPCGUtilsElementPoints::~FPCGUtilsElementPoints() = default;

void FPCGUtilsElementPoints::Reset()
{
	ElementIds.Reset();
	ElementEnds.Reset();
	Transforms.Reset();
	LocalBounds.Reset();
	bAnyBounds = false;

	FScopeLock Lock(&PointDataLock);
	PointData.Reset();
	BuildCount = 0;
}

void FPCGUtilsElementPoints::Reserve(int32 NumElements, int32 NumPoints)
{
	ElementIds.Reserve(NumElements);
	ElementEnds.Reserve(NumElements);
	Transforms.Reserve(NumPoints);
	LocalBounds.Reserve(NumPoints);
}

int32 FPCGUtilsElementPoints::BeginElement(int32 ElementId)
{
	ElementIds.Add(ElementId);
	return ElementEnds.Add(Transforms.Num());
}

void FPCGUtilsElementPoints::AddPoint(const FTransform& Transform, const FBox& InLocalBounds)
{
	check(!ElementEnds.IsEmpty());
	Transforms.Add(Transform);
	// An invalid (never set) box means no bounds, the same as the zero box.
	const FBox Bounds = InLocalBounds.IsValid ? InLocalBounds : FBox(FVector::ZeroVector, FVector::ZeroVector);
	LocalBounds.Add(Bounds);
	bAnyBounds |= !Bounds.Min.IsZero() || !Bounds.Max.IsZero();
	ElementEnds.Last() = Transforms.Num();
}

void FPCGUtilsElementPoints::AddPoint(const FVector& WorldPosition)
{
	check(!ElementEnds.IsEmpty());
	Transforms.Emplace(WorldPosition);
	LocalBounds.Emplace(FVector::ZeroVector, FVector::ZeroVector);
	ElementEnds.Last() = Transforms.Num();
}

void FPCGUtilsElementPoints::Reduce(const TBitArray<>& PointMask, TBitArray<>& OutElementMask) const
{
	check(PointMask.Num() == NumPoints());
	OutElementMask.Init(false, NumElements());

	int32 First = 0;
	for (int32 Element = 0; Element < ElementEnds.Num(); ++Element)
	{
		const int32 End = ElementEnds[Element];
		const int32 Count = End - First;
		if (Count == 1)
		{
			OutElementMask[Element] = PointMask[First];
		}
		else if (Count > 1)
		{
			int32 Selected = 0;
			for (int32 Point = First; Point < End; ++Point)
			{
				Selected += PointMask[Point] ? 1 : 0;
			}
			switch (Match)
			{
			case EPCGUtilsElementPointMatch::Any:
				OutElementMask[Element] = Selected > 0;
				break;
			case EPCGUtilsElementPointMatch::All:
				OutElementMask[Element] = Selected == Count;
				break;
			case EPCGUtilsElementPointMatch::AtLeastFraction:
				OutElementMask[Element] =
					static_cast<double>(Selected) + UE_DOUBLE_KINDA_SMALL_NUMBER >= MatchFraction * static_cast<double>(Count);
				break;
			}
		}
		First = End;
	}
}

const UPCGBasePointData* FPCGUtilsElementPoints::GetOrBuildPointData(FPCGContext* Context) const
{
	FScopeLock Lock(&PointDataLock);
	if (!PointData.IsValid())
	{
		UPCGBasePointData* Built = Builder ? Builder(*this, Context) : BuildDefaultPointData(*this, Context);
		PointData.Reset(Built);
		if (Built)
		{
			++BuildCount;
			++PCGUtilsElementPointsPrivate::TotalPointDataBuilds;
		}
	}
	return PointData.Get();
}

int32 FPCGUtilsElementPoints::GetTotalPointDataBuildCount()
{
	return PCGUtilsElementPointsPrivate::TotalPointDataBuilds.load();
}

UPCGBasePointData* FPCGUtilsElementPoints::BuildDefaultPointData(const FPCGUtilsElementPoints& Points, FPCGContext* Context)
{
	// May be reached from a worker of a parallel run (a selector evaluated per mesh), so allocate as those do.
	UPCGPointArrayData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
	const int32 Num = Points.NumPoints();
	Out->SetNumPoints(Num, /*bInitializeValues=*/true);
	Out->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

	TPCGValueRange<FTransform> OutTransforms = Out->GetTransformValueRange();
	TPCGValueRange<FVector> OutMins = Out->GetBoundsMinValueRange();
	TPCGValueRange<FVector> OutMaxs = Out->GetBoundsMaxValueRange();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		OutTransforms[Index] = Points.Transforms[Index];
		OutMins[Index] = Points.LocalBounds[Index].Min;
		OutMaxs[Index] = Points.LocalBounds[Index].Max;
	}
	return Out;
}
