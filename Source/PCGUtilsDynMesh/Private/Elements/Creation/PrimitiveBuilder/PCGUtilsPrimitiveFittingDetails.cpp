// Copyright Max Harris
// Fitting/alignment structures adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"

namespace PCGUtilsFitting
{
	void ScaleToFitAxis(
		const EPCGUtilsScaleToFit Fit, const int32 Axis, const FVector& TargetScale, const FVector& TargetSize,
		const FVector& CandidateSize, const FVector& MinMaxFit, FVector& OutScale)
	{
		const double Scale = TargetScale[Axis];
		double FinalScale = Scale;

		switch (Fit)
		{
		default: case EPCGUtilsScaleToFit::None:
			break;
		case EPCGUtilsScaleToFit::Fill:
			FinalScale = (TargetSize[Axis] * Scale) / CandidateSize[Axis];
			break;
		case EPCGUtilsScaleToFit::Min:
			FinalScale = MinMaxFit[0];
			break;
		case EPCGUtilsScaleToFit::Max:
			FinalScale = MinMaxFit[1];
			break;
		case EPCGUtilsScaleToFit::Avg:
			FinalScale = MinMaxFit[2];
			break;
		}

		OutScale[Axis] = FinalScale;
	}

	void ScaleToFitAxes(
		const EPCGUtilsFitMode Mode, const EPCGUtilsScaleToFit UniformFit,
		const EPCGUtilsScaleToFit FitX, const EPCGUtilsScaleToFit FitY, const EPCGUtilsScaleToFit FitZ,
		const FVector& TargetSize, const FVector& TargetScale, const FBox& InBounds, FVector& OutScale)
	{
		if (Mode == EPCGUtilsFitMode::None)
		{
			return;
		}

		const FVector TargetSizeScaled = TargetSize * TargetScale;
		const FVector CandidateSize = InBounds.GetSize();

		const double XFactor = CandidateSize.X != 0.0 ? TargetSizeScaled.X / CandidateSize.X : 1.0;
		const double YFactor = CandidateSize.Y != 0.0 ? TargetSizeScaled.Y / CandidateSize.Y : 1.0;
		const double ZFactor = CandidateSize.Z != 0.0 ? TargetSizeScaled.Z / CandidateSize.Z : 1.0;

		// Pack all three uniform scale options into a single FVector:
		// X = smallest axis ratio (Min), Y = largest (Max), Z = average (Avg).
		const FVector FitMinMax = FVector(
			FMath::Min3(XFactor, YFactor, ZFactor), FMath::Max3(XFactor, YFactor, ZFactor),
			(XFactor + YFactor + ZFactor) / 3.0);

		if (Mode == EPCGUtilsFitMode::Uniform)
		{
			ScaleToFitAxis(UniformFit, 0, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(UniformFit, 1, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(UniformFit, 2, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
		}
		else
		{
			ScaleToFitAxis(FitX, 0, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(FitY, 1, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
			ScaleToFitAxis(FitZ, 2, TargetScale, TargetSize, CandidateSize, FitMinMax, OutScale);
		}
	}

	void JustifyAxis(
		const EPCGUtilsJustifyFrom From, const EPCGUtilsJustifyTo To,
		const double FromValue, const double ToValue,
		const int32 Axis,
		const FVector& InCenter, const FVector& InSize,
		const FVector& OutCenter, const FVector& OutSize,
		FVector& OutTranslation)
	{
		double Start = 0;
		double End = 0;

		const double HalfOutSize = OutSize[Axis] * 0.5;
		const double HalfInSize = InSize[Axis] * 0.5;

		switch (From)
		{
		default: case EPCGUtilsJustifyFrom::Min:
			Start = OutCenter[Axis] - HalfOutSize;
			break;
		case EPCGUtilsJustifyFrom::Center:
			Start = OutCenter[Axis];
			break;
		case EPCGUtilsJustifyFrom::Max:
			Start = OutCenter[Axis] + HalfOutSize;
			break;
		case EPCGUtilsJustifyFrom::Custom:
			Start = OutCenter[Axis] - HalfOutSize + (OutSize[Axis] * FromValue);
			break;
		case EPCGUtilsJustifyFrom::Pivot:
			Start = 0;
			break;
		}

		switch (To)
		{
		default: case EPCGUtilsJustifyTo::Min:
			End = InCenter[Axis] - HalfInSize;
			break;
		case EPCGUtilsJustifyTo::Center:
			End = InCenter[Axis];
			break;
		case EPCGUtilsJustifyTo::Max:
			End = InCenter[Axis] + HalfInSize;
			break;
		case EPCGUtilsJustifyTo::Custom:
			End = InCenter[Axis] - HalfInSize + (InSize[Axis] * ToValue);
			break;
		case EPCGUtilsJustifyTo::Same:
			switch (From)
			{
			default: case EPCGUtilsJustifyFrom::Min:
				End = InCenter[Axis] - HalfInSize;
				break;
			case EPCGUtilsJustifyFrom::Center:
				End = InCenter[Axis];
				break;
			case EPCGUtilsJustifyFrom::Max:
				End = InCenter[Axis] + HalfInSize;
				break;
			case EPCGUtilsJustifyFrom::Custom:
				End = InCenter[Axis] - HalfInSize + (InSize[Axis] * FromValue);
				break;
			case EPCGUtilsJustifyFrom::Pivot:
				End = 0;
				break;
			}
			break;
		case EPCGUtilsJustifyTo::Pivot:
			End = 0;
			break;
		}

		OutTranslation[Axis] = End - Start;
	}
}

void FPCGUtilsScaleToFitDetails::Process(const FVector& TargetSize, const FVector& TargetScale, const FBox& InBounds, FVector& OutScale) const
{
	OutScale = TargetScale;
	PCGUtilsFitting::ScaleToFitAxes(ScaleToFitMode, ScaleToFit, ScaleToFitX, ScaleToFitY, ScaleToFitZ, TargetSize, TargetScale, InBounds, OutScale);
}

void FPCGUtilsSingleJustifyDetails::JustifyAxis(const int32 Axis, const FVector& InCenter, const FVector& InSize, const FVector& OutCenter, const FVector& OutSize, FVector& OutTranslation) const
{
	PCGUtilsFitting::JustifyAxis(From, To, FromValue, ToValue, Axis, InCenter, InSize, OutCenter, OutSize, OutTranslation);
}

void FPCGUtilsJustificationDetails::Process(const FBox& TargetBounds, const FBox& FittedBounds, FVector& OutTranslation) const
{
	const FVector InCenter = TargetBounds.GetCenter();
	const FVector InSize = TargetBounds.GetSize();

	const FVector OutCenter = FittedBounds.GetCenter();
	const FVector OutSize = FittedBounds.GetSize();

	if (bDoJustifyX)
	{
		JustifyX.JustifyAxis(0, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
	if (bDoJustifyY)
	{
		JustifyY.JustifyAxis(1, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
	if (bDoJustifyZ)
	{
		JustifyZ.JustifyAxis(2, InCenter, InSize, OutCenter, OutSize, OutTranslation);
	}
}

void FPCGUtilsFittingDetails::ComputeLocalTransform(
	const FTransform& SeedTransform, const FBox& SeedLocalBounds, const FBox& CandidateBounds, FTransform& OutTransform) const
{
	// Padding insets (or, if negative, outsets) the seed bounds used as the fitting target, independently of
	// the primitive's own geometry. Clamp per axis so an over-large padding collapses to the bounds center
	// instead of inverting.
	FBox PaddedBounds = SeedLocalBounds;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Center = (PaddedBounds.Min[Axis] + PaddedBounds.Max[Axis]) * 0.5;
		PaddedBounds.Min[Axis] = FMath::Min(PaddedBounds.Min[Axis] + Padding[Axis], Center);
		PaddedBounds.Max[Axis] = FMath::Max(PaddedBounds.Max[Axis] - Padding[Axis], Center);
	}

	const FVector LocalScale = LocalTransform.GetScale3D();
	const FQuat LocalRotation = LocalTransform.GetRotation();
	const FVector LocalTranslation = LocalTransform.GetTranslation();

	// Scale the candidate's own bounds by the local pre-transform's scale before computing fit factors, so
	// LocalTransform's scale composes with (rather than fights) the fitting result.
	const FBox ScaledCandidateBounds(CandidateBounds.Min * LocalScale, CandidateBounds.Max * LocalScale);

	FVector OutScale = SeedTransform.GetScale3D();
	const FVector PaddedSize = PaddedBounds.GetSize();
	ScaleToFit.Process(PaddedSize, SeedTransform.GetScale3D(), ScaledCandidateBounds, OutScale);

	FBox FittedBounds(ScaledCandidateBounds.Min * OutScale, ScaledCandidateBounds.Max * OutScale);
	if (!LocalRotation.IsIdentity())
	{
		FittedBounds = FittedBounds.TransformBy(FTransform(LocalRotation));
	}

	FVector OutTranslation = FVector::ZeroVector;
	Justification.Process(PaddedBounds, FittedBounds, OutTranslation);

	OutTransform = SeedTransform;
	OutTransform.AddToTranslation(SeedTransform.GetRotation().RotateVector(OutTranslation));
	OutTransform.SetScale3D(OutScale);
	OutTransform.SetRotation(SeedTransform.GetRotation() * LocalRotation);

	if (!LocalTranslation.IsNearlyZero())
	{
		OutTransform.AddToTranslation(OutTransform.GetRotation().RotateVector(LocalTranslation));
	}
}
