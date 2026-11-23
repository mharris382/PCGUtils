// Copyright Max Harris

#include "Selectors/PCGUtilsInBoundsPointSelector.h"

#include "Data/PCGBasePointData.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsInBoundsPointSelector"

FPCGUtilsSelectorWorldBox::FPCGUtilsSelectorWorldBox(const FTransform& Transform, const FBox& LocalBounds)
{
	Center = Transform.TransformPosition(LocalBounds.GetCenter());
	const FQuat Rotation = Transform.GetRotation();
	Axis[0] = Rotation.GetAxisX();
	Axis[1] = Rotation.GetAxisY();
	Axis[2] = Rotation.GetAxisZ();
	Half = (LocalBounds.GetExtent() * Transform.GetScale3D()).GetAbs();
}

double FPCGUtilsSelectorWorldBox::Radius(const FVector& Direction) const
{
	return FMath::Abs(Axis[0] | Direction) * Half.X + FMath::Abs(Axis[1] | Direction) * Half.Y + FMath::Abs(Axis[2] | Direction) * Half.Z;
}

bool PCGUtilsSelectorBoxMaths::BoxContains(const FTransform& Transform, const FBox& LocalBounds, const FVector& WorldPosition)
{
	return LocalBounds.IsInsideOrOn(Transform.InverseTransformPosition(WorldPosition));
}

bool PCGUtilsSelectorBoxMaths::BoxesOverlap(const FPCGUtilsSelectorWorldBox& A, const FPCGUtilsSelectorWorldBox& B)
{
	const FVector Between = B.Center - A.Center;
	auto Separates = [&A, &B, &Between](const FVector& Direction)
	{
		return FMath::Abs(Between | Direction) > A.Radius(Direction) + B.Radius(Direction) + UE_DOUBLE_KINDA_SMALL_NUMBER;
	};

	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Separates(A.Axis[Index]) || Separates(B.Axis[Index]))
		{
			return false;
		}
	}
	for (int32 IndexA = 0; IndexA < 3; ++IndexA)
	{
		for (int32 IndexB = 0; IndexB < 3; ++IndexB)
		{
			FVector Cross = A.Axis[IndexA] ^ B.Axis[IndexB];
			// Parallel axes give no new direction; the face axes above already cover it.
			if (Cross.SizeSquared() < UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Cross.Normalize();
			if (Separates(Cross))
			{
				return false;
			}
		}
	}
	return true;
}

namespace PCGUtilsInBoundsPointSelectorPrivate
{
	using FPrepared = UPCGUtilsInBoundsPointSelectorData::FPrepared;
	using FBound = UPCGUtilsInBoundsPointSelectorData::FBound;

	bool AnyBoundContains(const FPrepared& Prepared, const FVector& WorldPosition)
	{
		if (!Prepared.Reach.IsInsideOrOn(WorldPosition))
		{
			return false;
		}
		for (const FBound& Bound : Prepared.Bounds)
		{
			if (PCGUtilsSelectorBoxMaths::BoxContains(Bound.Transform, Bound.LocalBounds, WorldPosition))
			{
				return true;
			}
		}
		return false;
	}

	class FInBoundsOperation final : public FPCGUtilsPointSelectorOperation
	{
	public:
		FInBoundsOperation(TSharedRef<const FPrepared> InPrepared, EPCGUtilsSelectorContainment InContainment)
			: Prepared(MoveTemp(InPrepared)), Containment(InContainment)
		{
		}

		virtual bool Select(const FPCGUtilsElementPoints& Points, FPCGContext* Context, TBitArray<>& OutMask) const override
		{
			const int32 Num = Points.NumPoints();
			OutMask.Init(false, Num);
			if (Prepared->Bounds.IsEmpty())
			{
				return true;
			}

			// Transforms and bounds are all this selector reads: it never asks for point data.
			const TArray<FTransform>& Transforms = Points.GetTransforms();
			const TArray<FBox>& LocalBounds = Points.GetLocalBounds();
			const bool bPositionsOnly = Containment == EPCGUtilsSelectorContainment::Center || !Points.HasAnyBounds();

			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FTransform& Transform = Transforms[Index];
				const FBox& Box = LocalBounds[Index];
				// A point with no bounds is its position: Center, Any Part and Fully Inside are then one test.
				if (bPositionsOnly || (Box.Min.IsZero() && Box.Max.IsZero()))
				{
					OutMask[Index] = AnyBoundContains(*Prepared, Transform.GetLocation());
					continue;
				}

				const FPCGUtilsSelectorWorldBox WorldBox(Transform, Box);
				const double Reach = WorldBox.Half.Size();
				if (!Prepared->Reach.ExpandBy(Reach).IsInsideOrOn(WorldBox.Center))
				{
					continue;
				}

				if (Containment == EPCGUtilsSelectorContainment::AnyPart)
				{
					for (const FBound& Bound : Prepared->Bounds)
					{
						if (PCGUtilsSelectorBoxMaths::BoxesOverlap(Bound.WorldBox, WorldBox))
						{
							OutMask[Index] = true;
							break;
						}
					}
				}
				else
				{
					// Fully Inside: all eight corners in one and the same bound.
					FVector Corners[8];
					Box.GetVertices(Corners);
					for (FVector& Corner : Corners)
					{
						Corner = Transform.TransformPosition(Corner);
					}
					for (const FBound& Bound : Prepared->Bounds)
					{
						bool bAllInside = true;
						for (const FVector& Corner : Corners)
						{
							if (!PCGUtilsSelectorBoxMaths::BoxContains(Bound.Transform, Bound.LocalBounds, Corner))
							{
								bAllInside = false;
								break;
							}
						}
						if (bAllInside)
						{
							OutMask[Index] = true;
							break;
						}
					}
				}
			}
			return true;
		}

	private:
		TSharedRef<const FPrepared> Prepared;
		EPCGUtilsSelectorContainment Containment;
	};
}

TSharedPtr<const UPCGUtilsInBoundsPointSelectorData::FPrepared> UPCGUtilsInBoundsPointSelectorData::BuildPrepared() const
{
	TSharedRef<FPrepared> Out = MakeShared<FPrepared>();
	for (const UPCGBasePointData* Points : PointData)
	{
		if (!Points)
		{
			continue;
		}
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> Mins = Points->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> Maxs = Points->GetConstBoundsMaxValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FTransform& Transform = Transforms[Index];
			// Expand is in world units; the bound is in the point's local space, so divide by its scale.
			const FVector Scale = Transform.GetScale3D().GetAbs();
			const FVector LocalExpand(
				Scale.X > UE_DOUBLE_SMALL_NUMBER ? Expand / Scale.X : 0.0,
				Scale.Y > UE_DOUBLE_SMALL_NUMBER ? Expand / Scale.Y : 0.0,
				Scale.Z > UE_DOUBLE_SMALL_NUMBER ? Expand / Scale.Z : 0.0);
			const FVector Min = Mins[Index] - LocalExpand;
			const FVector Max = Maxs[Index] + LocalExpand;
			if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z)
			{
				++Out->NumSkipped;
				continue;
			}

			FBound& Bound = Out->Bounds.AddDefaulted_GetRef();
			Bound.Transform = Transform;
			Bound.LocalBounds = FBox(Min, Max);
			Bound.WorldBox = FPCGUtilsSelectorWorldBox(Transform, Bound.LocalBounds);
			Out->Reach += Bound.LocalBounds.TransformBy(Transform);
		}
	}
	return Out;
}

bool UPCGUtilsInBoundsPointSelectorData::Prepare(FPCGContext* InContext)
{
	Prepared = BuildPrepared();
	if (Prepared->NumSkipped > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvertedBounds", "Selector | In Bounds: skipped {0} point(s) on the Bounds pin whose bounds are inside out (a Bounds Max below its Bounds Min, after Expand {1})."),
			FText::AsNumber(Prepared->NumSkipped), FText::AsNumber(Expand)), InContext);
	}
	return true;
}

TSharedPtr<const FPCGUtilsPointSelectorOperation> UPCGUtilsInBoundsPointSelectorData::CreateOperation(FPCGContext* Context) const
{
	// Prepare is the normal path; a selector made by hand and never prepared still works, without the warning.
	const TSharedPtr<const FPrepared> Bounds = Prepared ? Prepared : BuildPrepared();
	return MakeShared<PCGUtilsInBoundsPointSelectorPrivate::FInBoundsOperation>(Bounds.ToSharedRef(), Containment);
}

void UPCGUtilsInBoundsPointSelectorData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	uint8 LocalContainment = static_cast<uint8>(Containment);
	double LocalExpand = Expand;
	Ar << LocalContainment;
	Ar << LocalExpand;
	// The bound points are data dependencies (added by the node), so the base already folded their Crcs in.
}

#undef LOCTEXT_NAMESPACE
