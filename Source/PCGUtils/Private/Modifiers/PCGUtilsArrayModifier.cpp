// Copyright Max Harris

#include "Modifiers/PCGUtilsArrayModifier.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGSplineData.h"
#include "Modifiers/PCGUtilsModifierStack.h"

#define LOCTEXT_NAMESPACE "PCGUtilsArrayModifier"

namespace PCGUtilsArrayModifier
{
	/** Hard cap so a mistyped Length or a tiny step cannot allocate millions of copies. */
	constexpr int32 MaxCopies = 4096;
	constexpr double ZeroStep = 1e-6;

	FMatrix RotationScaleMatrix(const FRotator& Rotation, const FVector& Scale)
	{
		// Row-vector convention: scale first, then rotate.
		return FScaleMatrix(Scale) * FRotationMatrix(Rotation);
	}

	FMatrix Power(const FMatrix& Matrix, int32 Exponent)
	{
		FMatrix Result = FMatrix::Identity;
		for (int32 Index = 0; Index < Exponent; ++Index)
		{
			Result = Result * Matrix;
		}
		return Result;
	}

	FVector PointLocation(const FMatrix& LocalToModifier)
	{
		return LocalToModifier.GetOrigin();
	}
}

FVector UPCGUtilsArrayModifier::AxisVector(EPCGUtilsModifierAxis Axis)
{
	switch (Axis)
	{
	case EPCGUtilsModifierAxis::X: return FVector::XAxisVector;
	case EPCGUtilsModifierAxis::Y: return FVector::YAxisVector;
	default: return FVector::ZAxisVector;
	}
}

FVector UPCGUtilsArrayModifier::GetStepTranslation(const FVector& BoundsSize) const
{
	FVector Step = FVector::ZeroVector;
	if (bUseRelativeOffset)
	{
		Step += RelativeOffset * BoundsSize;
	}
	if (bUseConstantOffset)
	{
		Step += ConstantOffset;
	}
	return Step;
}

int32 UPCGUtilsArrayModifier::ComputeCount(const FVector& BoundsSize, const FPCGUtilsModifierContext* Context) const
{
	using namespace PCGUtilsArrayModifier;

	int32 Result = Count;
	if (Mode != EPCGUtilsArrayMode::Circle && FitType == EPCGUtilsArrayFit::FitLength)
	{
		// Blender: the step length is the translation of the whole offset matrix.
		FVector StepTranslation = GetStepTranslation(BoundsSize);
		if (Mode == EPCGUtilsArrayMode::Transform)
		{
			StepTranslation += ObjectOffset;
		}
		const double Distance = StepTranslation.Size();
		if (Distance <= ZeroStep)
		{
			if (Context)
			{
				Context->Warn(this, LOCTEXT("ZeroStepFit", "Array: Fit Length needs a step longer than zero; the step translation is (0, 0, 0), so only the original is kept."));
			}
			Result = 1;
		}
		else
		{
			Result = FMath::FloorToInt32((Length + UE_KINDA_SMALL_NUMBER) / Distance) + 1;
		}
	}

	if (Result > MaxCopies)
	{
		if (Context)
		{
			Context->Warn(this, FText::Format(LOCTEXT("TooManyCopies", "Array: {0} copies were asked for; only the first {1} are made."),
				FText::AsNumber(Result), FText::AsNumber(MaxCopies)));
		}
		Result = MaxCopies;
	}
	return FMath::Max(Result, 1);
}

void UPCGUtilsArrayModifier::GetLineCopyParts(int32 CopyIndex, const FVector& BoundsSize, FMatrix& OutRotationScale, FVector& OutTranslation) const
{
	using namespace PCGUtilsArrayModifier;
	OutRotationScale = Power(RotationScaleMatrix(RotationStep, ScaleStep), CopyIndex);
	OutTranslation = GetStepTranslation(BoundsSize) * static_cast<double>(CopyIndex);
}

TArray<FMatrix> UPCGUtilsArrayModifier::ComputeCopyMatrices(const FVector& BoundsSize, const FPCGUtilsModifierContext* Context) const
{
	using namespace PCGUtilsArrayModifier;

	const int32 NumCopies = ComputeCount(BoundsSize, Context);
	TArray<FMatrix> Matrices;
	Matrices.Reserve(NumCopies);

	switch (Mode)
	{
	case EPCGUtilsArrayMode::Line:
		{
			for (int32 Copy = 0; Copy < NumCopies; ++Copy)
			{
				FMatrix RotationScale;
				FVector Translation;
				GetLineCopyParts(Copy, BoundsSize, RotationScale, Translation);
				Matrices.Add(RotationScale * FTranslationMatrix(Translation));
			}
			break;
		}
	case EPCGUtilsArrayMode::Transform:
		{
			// Blender: offset = T(D) * Step (column vectors), copy c = offset^c. In row vectors: Step then T(D).
			const FMatrix Step = RotationScaleMatrix(RotationStep, ScaleStep) * FTranslationMatrix(ObjectOffset);
			const FMatrix Offset = Step * FTranslationMatrix(GetStepTranslation(BoundsSize));
			FMatrix Current = FMatrix::Identity;
			for (int32 Copy = 0; Copy < NumCopies; ++Copy)
			{
				Matrices.Add(Current);
				Current = Current * Offset;
			}
			break;
		}
	case EPCGUtilsArrayMode::Circle:
		{
			const bool bFullRing = FMath::Abs(TotalAngle) >= 360.0 - UE_KINDA_SMALL_NUMBER;
			const int32 Divisions = bFullRing ? NumCopies : FMath::Max(NumCopies - 1, 1);
			const double StepRadians = FMath::DegreesToRadians(TotalAngle) / static_cast<double>(Divisions);
			const FVector Axis = AxisVector(CircleAxis);
			for (int32 Copy = 0; Copy < NumCopies; ++Copy)
			{
				const FQuat Rotation(Axis, StepRadians * Copy);
				Matrices.Add(FTranslationMatrix(-CircleCenter) * FQuatRotationMatrix(Rotation) * FTranslationMatrix(CircleCenter));
			}
			break;
		}
	}
	return Matrices;
}

void UPCGUtilsArrayModifier::ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const
{
	using namespace PCGUtilsArrayModifier;

	const int32 NumPoints = In ? In->GetNumPoints() : 0;
	if (NumPoints == 0)
	{
		Out.Add(const_cast<UPCGBasePointData*>(In));
		return;
	}

	// Each point in component space, and the input's bounds there (Blender uses the whole mesh's local bounds).
	const FMatrix WorldToModifier = Context.ModifierToWorld.ToInverseMatrixWithScale();
	const FMatrix ModifierToWorld = Context.ModifierToWorld.ToMatrixWithScale();
	TArray<FMatrix> LocalToModifier;
	LocalToModifier.Reserve(NumPoints);
	FBox Bounds(ForceInit);
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const FMatrix Matrix = In->GetTransform(Index).ToMatrixWithScale() * WorldToModifier;
		LocalToModifier.Add(Matrix);
		Bounds += In->GetLocalBounds(Index).TransformBy(Matrix);
	}
	const FVector BoundsSize = Bounds.IsValid ? Bounds.GetSize() : FVector::ZeroVector;

	const TArray<FMatrix> CopyMatrices = ComputeCopyMatrices(BoundsSize, &Context);
	const int32 NumCopies = CopyMatrices.Num();

	// Place every copy of every point, then drop merged ones.
	struct FPlaced
	{
		int32 Source;
		int32 Copy;
		FMatrix LocalToModifier;
	};
	TArray<FPlaced> Placed;
	Placed.Reserve(NumCopies * NumPoints);
	for (int32 Copy = 0; Copy < NumCopies; ++Copy)
	{
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FMatrix& Local = LocalToModifier[Index];
			FMatrix Result;
			if (Mode == EPCGUtilsArrayMode::Line)
			{
				// Linear position; rotation and scale compound about the point's own pivot.
				FMatrix RotationScale;
				FVector Translation;
				GetLineCopyParts(Copy, BoundsSize, RotationScale, Translation);
				const FVector Pivot = PointLocation(Local);
				Result = Local * FTranslationMatrix(-Pivot) * RotationScale * FTranslationMatrix(Pivot + Translation);
			}
			else if (Mode == EPCGUtilsArrayMode::Circle && !bAlignToRing)
			{
				const FVector Pivot = PointLocation(Local);
				Result = Local * FTranslationMatrix(CopyMatrices[Copy].TransformPosition(Pivot) - Pivot);
			}
			else
			{
				Result = Local * CopyMatrices[Copy];
			}
			Placed.Add({Index, Copy, Result});
		}
	}

	if (bMerge && NumCopies > 1)
	{
		const double MergeDistanceSquared = FMath::Square(MergeDistance);
		auto Location = [&](int32 Copy, int32 Index) { return Placed[Copy * NumPoints + Index].LocalToModifier.GetOrigin(); };
		TBitArray<> Keep(true, Placed.Num());
		for (int32 Copy = 1; Copy < NumCopies; ++Copy)
		{
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				if (FVector::DistSquared(Location(Copy, Index), Location(Copy - 1, Index)) <= MergeDistanceSquared)
				{
					Keep[Copy * NumPoints + Index] = false;
				}
			}
		}
		if (bMergeFirstAndLast && NumCopies > 2)
		{
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				if (FVector::DistSquared(Location(NumCopies - 1, Index), Location(0, Index)) <= MergeDistanceSquared)
				{
					Keep[(NumCopies - 1) * NumPoints + Index] = false;
				}
			}
		}
		TArray<FPlaced> Kept;
		Kept.Reserve(Placed.Num());
		for (int32 Index = 0; Index < Placed.Num(); ++Index)
		{
			if (Keep[Index])
			{
				Kept.Add(Placed[Index]);
			}
		}
		Placed = MoveTemp(Kept);
	}

	TArray<int32> SourceIndices;
	TArray<int32> CopyIndices;
	SourceIndices.Reserve(Placed.Num());
	CopyIndices.Reserve(Placed.Num());
	for (const FPlaced& Item : Placed)
	{
		SourceIndices.Add(Item.Source);
		CopyIndices.Add(Item.Copy);
	}

	UPCGBasePointData* Result = FPCGUtilsModifierHelpers::NewPointsFrom(Context, In, SourceIndices);
	TPCGValueRange<FTransform> Transforms = Result->GetTransformValueRange();
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		Transforms[Index] = FTransform(Placed[Index].LocalToModifier * ModifierToWorld);
	}

	if (bWriteCopyIndex)
	{
		FPCGUtilsModifierHelpers::WritePointInt(Result, CopyIndexAttributeName, CopyIndices);
	}
	Out.Add(Result);
}

void UPCGUtilsArrayModifier::ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const
{
	const FPCGUtilsModifierPath Source = FPCGUtilsModifierHelpers::ReadPath(Context, In);
	if (Source.Num() == 0)
	{
		Out.Add(const_cast<UPCGSplineData*>(In));
		return;
	}

	FBox Bounds(ForceInit);
	for (const FSplinePoint& Point : Source.Points)
	{
		Bounds += Point.Position;
	}
	const TArray<FMatrix> CopyMatrices = ComputeCopyMatrices(Bounds.GetSize(), &Context);

	for (int32 Copy = 0; Copy < CopyMatrices.Num(); ++Copy)
	{
		FPCGUtilsModifierPath CopyPath;
		CopyPath.bClosed = Source.bClosed;
		for (int32 Index = 0; Index < Source.Num(); ++Index)
		{
			CopyPath.Add(FPCGUtilsModifierHelpers::TransformSplinePoint(Source.Points[Index], CopyMatrices[Copy]), Source.SourceIndices[Index]);
		}
		UPCGSplineData* Result = FPCGUtilsModifierHelpers::WritePath(Context, In, CopyPath);
		if (bWriteCopyIndex)
		{
			FPCGUtilsModifierHelpers::WriteDataInt(Result, CopyIndexAttributeName, Copy);
		}
		Out.Add(Result);
	}
}

#undef LOCTEXT_NAMESPACE
