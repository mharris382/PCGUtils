// Copyright Max Harris
// Fitting/alignment structures adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"

#include "Serialization/ArchiveCrc32.h"

bool FPCGUtilsFittingOrientation::Validate(FString &Error) const
{
	if (static_cast<uint8>(AxisOrder) > 5)
	{
		Error = FString::Printf(TEXT("Orientation.AxisOrder is %d; expected an enum value in [0, 5]."),
		                        static_cast<uint8>(AxisOrder));
		return false;
	}
	if (static_cast<uint8>(RotationConstruction) > 8)
	{
		Error = FString::Printf(TEXT("Orientation.RotationConstruction is %d; expected an enum value in [0, 8]."),
		                        static_cast<uint8>(RotationConstruction));
		return false;
	}
	return true;
}
FQuat FPCGUtilsFittingOrientation::GetRotation() const
{
	// Axis permutation and MakeFrom dispatch adapted from PCGExMathAxis (MIT).
	static const int32 Orders[6][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}, {1, 0, 2}, {2, 1, 0}, {0, 2, 1}};
	const FVector Axes[3] = {FVector::ForwardVector, FVector::RightVector, FVector::UpVector};
	const int32 Order = FMath::Clamp(static_cast<int32>(AxisOrder), 0, 5);
	const FVector X = Axes[Orders[Order][0]], Y = Axes[Orders[Order][1]], Z = Axes[Orders[Order][2]];
	switch (RotationConstruction)
	{
	case EPCGUtilsMakeRotAxis::X:
		return FRotationMatrix::MakeFromX(X).ToQuat();
	case EPCGUtilsMakeRotAxis::XZ:
		return FRotationMatrix::MakeFromXZ(X, Z).ToQuat();
	case EPCGUtilsMakeRotAxis::Y:
		return FRotationMatrix::MakeFromY(Y).ToQuat();
	case EPCGUtilsMakeRotAxis::YX:
		return FRotationMatrix::MakeFromYX(Y, X).ToQuat();
	case EPCGUtilsMakeRotAxis::YZ:
		return FRotationMatrix::MakeFromYZ(Y, Z).ToQuat();
	case EPCGUtilsMakeRotAxis::Z:
		return FRotationMatrix::MakeFromZ(Z).ToQuat();
	case EPCGUtilsMakeRotAxis::ZX:
		return FRotationMatrix::MakeFromZX(Z, X).ToQuat();
	case EPCGUtilsMakeRotAxis::ZY:
		return FRotationMatrix::MakeFromZY(Z, Y).ToQuat();
	default:
		return FRotationMatrix::MakeFromXY(X, Y).ToQuat();
	}
}

void FPCGUtilsFittingOrientation::RemapTarget(FTransform &Frame, FBox &Bounds) const
{
	const FQuat Rotation = GetRotation();
	const FVector OldScale = Frame.GetScale3D();
	const FVector Axes[3] = {Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ()};
	FVector Scale;
	for (int32 Axis = 0; Axis < 3; ++Axis)
		Scale[Axis] = FVector::DotProduct(Axes[Axis].GetAbs(), OldScale);
	Bounds = Bounds.TransformBy(FTransform(Rotation.Inverse()));
	Frame.SetRotation(Frame.GetRotation() * Rotation);
	Frame.SetScale3D(Scale);
}
namespace PCGUtilsFitting
{
void ApplyPadding(FBox &InOutBounds, const FVector &PaddingMin, const FVector &PaddingMax)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Center = (InOutBounds.Min[Axis] + InOutBounds.Max[Axis]) * 0.5;
		InOutBounds.Min[Axis] = FMath::Min(InOutBounds.Min[Axis] + PaddingMin[Axis], Center);
		InOutBounds.Max[Axis] = FMath::Max(InOutBounds.Max[Axis] - PaddingMax[Axis], Center);
	}
}

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
    } // namespace PCGUtilsFitting

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
	if (!Orientation.GetRotation().IsIdentity())
	{
		FTransform TargetFrame = SeedTransform;
		FBox TargetBounds = SeedLocalBounds;
		PCGUtilsFitting::ApplyPadding(TargetBounds, PaddingMin, PaddingMax);
		Orientation.RemapTarget(TargetFrame, TargetBounds);
		FPCGUtilsFittingDetails Remapped = *this;
		Remapped.Orientation = FPCGUtilsFittingOrientation();
		Remapped.PaddingMin = Remapped.PaddingMax = FVector::ZeroVector;
		Remapped.ComputeLocalTransform(TargetFrame, TargetBounds, CandidateBounds, OutTransform);
		return;
	}

	// Padding insets (or, if negative, outsets) the seed bounds used as the fitting target, independently of
	// the primitive's own geometry. Each side of each axis moves on its own: PaddingMin pushes the min corner
	// inwards, PaddingMax pushes the max corner inwards. Clamp per axis so over-large padding collapses to the
	// bounds center instead of inverting.
	FBox PaddedBounds = SeedLocalBounds;
	PCGUtilsFitting::ApplyPadding(PaddedBounds, PaddingMin, PaddingMax);

	const FVector LocalScale = LocalTransform.GetScale3D();
	const FQuat LocalRotation = LocalTransform.GetRotation();
	const FVector LocalTranslation = LocalTransform.GetTranslation();

	// Scale the candidate's own bounds by the local pre-transform's scale before computing fit factors, so
	// LocalTransform's scale composes with (rather than fights) the fitting result.
	const FBox ScaledCandidateBounds = CandidateBounds.TransformBy(FTransform(FQuat::Identity, FVector::ZeroVector, LocalScale));

	FVector OutScale = SeedTransform.GetScale3D();
	const FVector PaddedSize = PaddedBounds.GetSize();
	ScaleToFit.Process(PaddedSize, SeedTransform.GetScale3D(), ScaledCandidateBounds, OutScale);

	FBox FittedBounds = ScaledCandidateBounds.TransformBy(FTransform(FQuat::Identity, FVector::ZeroVector, OutScale));
	if (!LocalRotation.IsIdentity())
	{
		FittedBounds = FittedBounds.TransformBy(FTransform(LocalRotation));
	}

	FVector OutTranslation = FVector::ZeroVector;
	const FBox PhysicalTargetBounds = PaddedBounds.TransformBy(FTransform(FQuat::Identity, FVector::ZeroVector, SeedTransform.GetScale3D()));
	Justification.Process(PhysicalTargetBounds, FittedBounds, OutTranslation);

	OutTransform = SeedTransform;
	OutTransform.AddToTranslation(SeedTransform.GetRotation().RotateVector(OutTranslation));
	OutTransform.SetScale3D(OutScale * LocalScale);
	OutTransform.SetRotation(SeedTransform.GetRotation() * LocalRotation);

	if (!LocalTranslation.IsNearlyZero())
	{
		OutTransform.AddToTranslation(OutTransform.GetRotation().RotateVector(LocalTranslation));
	}
}

FTransform FPCGUtilsBoundsRelativeTransformDetails::ComputeTransform(const FBox& TargetBounds) const
{
	if (!TargetBounds.IsValid)
	{
		return LocalTransform;
	}

	FBox PaddedBounds = TargetBounds;
	PCGUtilsFitting::ApplyPadding(PaddedBounds, PaddingMin, PaddingMax);

	// A plane frame is an origin, not a finite candidate. A zero-size box makes Builder's From choices all
	// converge on that origin while retaining the familiar per-axis To choices against the target bounds.
	const FBox OriginBounds(FVector::ZeroVector, FVector::ZeroVector);
	FVector AlignedLocation = FVector::ZeroVector;
	Alignment.Process(PaddedBounds, OriginBounds, AlignedLocation);

	FTransform Result = LocalTransform;
	Result.SetLocation(AlignedLocation);
	Result.AddToTranslation(Result.GetRotation().RotateVector(LocalTransform.GetLocation()));
	return Result;
}

void FPCGUtilsBoundsRelativeTransformDetails::AddToCrc(FArchiveCrc32& Ar) const
{
	FPCGUtilsBoundsRelativeTransformDetails Local = *this;
	uint8 JustifyXFrom = static_cast<uint8>(Local.Alignment.JustifyX.From);
	uint8 JustifyXTo = static_cast<uint8>(Local.Alignment.JustifyX.To);
	uint8 JustifyYFrom = static_cast<uint8>(Local.Alignment.JustifyY.From);
	uint8 JustifyYTo = static_cast<uint8>(Local.Alignment.JustifyY.To);
	uint8 JustifyZFrom = static_cast<uint8>(Local.Alignment.JustifyZ.From);
	uint8 JustifyZTo = static_cast<uint8>(Local.Alignment.JustifyZ.To);
	Ar << Local.Alignment.bDoJustifyX << JustifyXFrom << JustifyXTo
		<< Local.Alignment.JustifyX.FromValue << Local.Alignment.JustifyX.ToValue;
	Ar << Local.Alignment.bDoJustifyY << JustifyYFrom << JustifyYTo
		<< Local.Alignment.JustifyY.FromValue << Local.Alignment.JustifyY.ToValue;
	Ar << Local.Alignment.bDoJustifyZ << JustifyZFrom << JustifyZTo
		<< Local.Alignment.JustifyZ.FromValue << Local.Alignment.JustifyZ.ToValue;
	FVector LocalPaddingMin = Local.PaddingMin;
	FVector LocalPaddingMax = Local.PaddingMax;
	FVector Location = Local.LocalTransform.GetLocation();
	FQuat Rotation = Local.LocalTransform.GetRotation();
	FVector Scale = Local.LocalTransform.GetScale3D();
	Ar << LocalPaddingMin << LocalPaddingMax << Location << Rotation << Scale;
}
