// Copyright Max Harris
#include "Elements/Painters/PCGDynMeshPainterMath.h"

FPCGUtilsDynMeshPainterValue PCGUtilsPainters::BlendValues(
	const FPCGUtilsDynMeshPainterValue& Base, const FPCGUtilsDynMeshPainterValue& Blend,
	EPCGUtilsDynMeshPainterMathOperation Operation, float Factor)
{
	using EType = EPCGUtilsDynMeshPainterValueType;
	using EChannel = EPCGUtilsDynMeshPainterColorChannel;
	const float Weight = FMath::IsFinite(Factor) ? FMath::Clamp(Factor, 0.0f, 1.0f) : 0.0f;
	auto Apply = [Operation, Weight](float A, float B)
	{
		if (Weight == 0.0f) { return A; }
		float Result;
		switch (Operation)
		{
		case EPCGUtilsDynMeshPainterMathOperation::Add: Result = A + B; break;
		case EPCGUtilsDynMeshPainterMathOperation::Subtract: Result = A - B; break;
		case EPCGUtilsDynMeshPainterMathOperation::Min: Result = FMath::Min(A, B); break;
		case EPCGUtilsDynMeshPainterMathOperation::Max: Result = FMath::Max(A, B); break;
		case EPCGUtilsDynMeshPainterMathOperation::Mix: Result = B; break;
		case EPCGUtilsDynMeshPainterMathOperation::Screen: Result = 1.0f - (1.0f - A) * (1.0f - B); break;
		default: Result = A * B; break;
		}
		return FMath::Lerp(A, Result, Weight);
	};
	if (Base.Type == EType::Scalar && Blend.Type == EType::Scalar)
	{
		return FPCGUtilsDynMeshPainterValue::MakeScalar(Apply(Base.Scalar, Blend.Scalar));
	}
	// The base defines the output channels. An untargeted scalar takes the color operand's channel set.
	const EChannel Channels = Base.Type == EType::Color ? Base.ColorChannels : Blend.ColorChannels;
	FVector4f Result = Base.Type == EType::Color ? Base.Color : FVector4f(Base.Scalar, Base.Scalar, Base.Scalar, Base.Scalar);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const EChannel Channel = static_cast<EChannel>(1 << Index);
		if (EnumHasAnyFlags(Channels, Channel) && (Blend.Type == EType::Scalar || EnumHasAnyFlags(Blend.ColorChannels, Channel)))
		{
			Result[Index] = Apply(Result[Index], Blend.Type == EType::Scalar ? Blend.Scalar : Blend.Color[Index]);
		}
	}
	return FPCGUtilsDynMeshPainterValue::MakeColor(Result, Channels);
}
