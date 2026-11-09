// Copyright Max Harris

#include "Elements/Metadata/PCGMetadataStepOpElement.h"

#include "PCGPin.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Utils/PCGPreconfiguration.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMetadataStepOpElement)

#define LOCTEXT_NAMESPACE "PCGMetadataStepOpElement"

namespace PCGMetadataStepOp
{
	const FName EdgeLabel = TEXT("Edge");

	/** Scalar SmoothStep, computed in double precision then cast back to the attribute type. */
	inline double SmoothStepScalar(const double Min, const double Max, const double Value)
	{
		const double Range = Max - Min;

		// Degenerate range: the transition has no width, so this becomes a hard step, mirroring HLSL smoothstep
		// behaviour for Value outside of [Min, Max].
		const double Alpha = FMath::IsNearlyZero(Range) ? ((Value >= Max) ? 1.0 : 0.0) : FMath::Clamp((Value - Min) / Range, 0.0, 1.0);

		return Alpha * Alpha * (3.0 - 2.0 * Alpha);
	}

	/**
	 * Scalar Multi Step: the SmoothStep alpha quantized down to StepCount equal steps, so the result takes the
	 * values 0, 1/StepCount ... (StepCount - 1)/StepCount inside the range and 1 at or above Max.
	 */
	// Not inlined: once inlined into a caller with constant arguments, MSVC reports a false C4723 (divide by 0) on the
	// guarded division below, which fails the Development build.
	FORCENOINLINE double MultiStepScalar(const double Min, const double Max, const double Value, const int32 StepCount)
	{
		const double Range = Max - Min;
		if (FMath::IsNearlyZero(Range))
		{
			return (Value >= Max) ? 1.0 : 0.0;
		}

		const double Steps = static_cast<double>(FMath::Max(1, StepCount));
		const double Alpha = FMath::Clamp((Value - Min) / Range, 0.0, 1.0);

		// The small bias keeps a value sitting exactly on a step boundary (0.6 of the range with 5 steps, say)
		// from landing one step low through floating point error.
		return FMath::Min(FMath::FloorToDouble(Alpha * Steps + 1e-9), Steps) / Steps;
	}

	/** Scalar Step, computed in double precision then cast back to the attribute type. */
	inline double StepScalar(const double Edge, const double Value)
	{
		return (Value >= Edge) ? 1.0 : 0.0;
	}

	template <typename T>
	inline T SmoothStep(const T& Min, const T& Max, const T& Value)
	{
		return static_cast<T>(SmoothStepScalar(static_cast<double>(Min), static_cast<double>(Max), static_cast<double>(Value)));
	}

	template <>
	inline FVector2D SmoothStep(const FVector2D& Min, const FVector2D& Max, const FVector2D& Value)
	{
		return FVector2D(
			SmoothStepScalar(Min.X, Max.X, Value.X),
			SmoothStepScalar(Min.Y, Max.Y, Value.Y));
	}

	template <>
	inline FVector SmoothStep(const FVector& Min, const FVector& Max, const FVector& Value)
	{
		return FVector(
			SmoothStepScalar(Min.X, Max.X, Value.X),
			SmoothStepScalar(Min.Y, Max.Y, Value.Y),
			SmoothStepScalar(Min.Z, Max.Z, Value.Z));
	}

	template <>
	inline FVector4 SmoothStep(const FVector4& Min, const FVector4& Max, const FVector4& Value)
	{
		return FVector4(
			SmoothStepScalar(Min.X, Max.X, Value.X),
			SmoothStepScalar(Min.Y, Max.Y, Value.Y),
			SmoothStepScalar(Min.Z, Max.Z, Value.Z),
			SmoothStepScalar(Min.W, Max.W, Value.W));
	}

	template <typename T>
	inline T MultiStep(const T& Min, const T& Max, const T& Value, const int32 StepCount)
	{
		return static_cast<T>(MultiStepScalar(static_cast<double>(Min), static_cast<double>(Max), static_cast<double>(Value), StepCount));
	}

	template <>
	inline FVector2D MultiStep(const FVector2D& Min, const FVector2D& Max, const FVector2D& Value, const int32 StepCount)
	{
		return FVector2D(
			MultiStepScalar(Min.X, Max.X, Value.X, StepCount),
			MultiStepScalar(Min.Y, Max.Y, Value.Y, StepCount));
	}

	template <>
	inline FVector MultiStep(const FVector& Min, const FVector& Max, const FVector& Value, const int32 StepCount)
	{
		return FVector(
			MultiStepScalar(Min.X, Max.X, Value.X, StepCount),
			MultiStepScalar(Min.Y, Max.Y, Value.Y, StepCount),
			MultiStepScalar(Min.Z, Max.Z, Value.Z, StepCount));
	}

	template <>
	inline FVector4 MultiStep(const FVector4& Min, const FVector4& Max, const FVector4& Value, const int32 StepCount)
	{
		return FVector4(
			MultiStepScalar(Min.X, Max.X, Value.X, StepCount),
			MultiStepScalar(Min.Y, Max.Y, Value.Y, StepCount),
			MultiStepScalar(Min.Z, Max.Z, Value.Z, StepCount),
			MultiStepScalar(Min.W, Max.W, Value.W, StepCount));
	}

	template <typename T>
	inline T Step(const T& Edge, const T& Value)
	{
		return static_cast<T>(StepScalar(static_cast<double>(Edge), static_cast<double>(Value)));
	}

	template <>
	inline FVector2D Step(const FVector2D& Edge, const FVector2D& Value)
	{
		return FVector2D(
			StepScalar(Edge.X, Value.X),
			StepScalar(Edge.Y, Value.Y));
	}

	template <>
	inline FVector Step(const FVector& Edge, const FVector& Value)
	{
		return FVector(
			StepScalar(Edge.X, Value.X),
			StepScalar(Edge.Y, Value.Y),
			StepScalar(Edge.Z, Value.Z));
	}

	template <>
	inline FVector4 Step(const FVector4& Edge, const FVector4& Value)
	{
		return FVector4(
			StepScalar(Edge.X, Value.X),
			StepScalar(Edge.Y, Value.Y),
			StepScalar(Edge.Z, Value.Z),
			StepScalar(Edge.W, Value.W));
	}
}

FName UPCGMetadataStepOpSettings::GetInputPinLabel(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return PCGPinConstants::DefaultInputLabel;
	case 1:
		return (Operation == EPCGMetadataStepOperation::Step) ? PCGMetadataStepOp::EdgeLabel : PCGMetadataSettingsBaseConstants::ClampMinLabel;
	case 2:
		return (Operation == EPCGMetadataStepOperation::Step) ? NAME_None : PCGMetadataSettingsBaseConstants::ClampMaxLabel;
	default:
		return NAME_None;
	}
}

uint32 UPCGMetadataStepOpSettings::GetOperandNum() const
{
	return (Operation == EPCGMetadataStepOperation::Step) ? 2 : 3;
}

FPCGAttributePropertyInputSelector UPCGMetadataStepOpSettings::GetInputSource(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return InputSource1;
	case 1:
		return InputSource2;
	case 2:
		return (Operation == EPCGMetadataStepOperation::Step) ? FPCGAttributePropertyInputSelector() : InputSource3;
	default:
		return FPCGAttributePropertyInputSelector();
	}
}

bool UPCGMetadataStepOpSettings::IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const
{
	bHasSpecialRequirement = false;
	return PCG::Private::IsOfTypes<float, double, int32, int64, FVector2D, FVector, FVector4>(TypeId);
}

uint16 UPCGMetadataStepOpSettings::GetOutputType(uint16 InputTypeId) const
{
	// Every operation returns a value in [0, 1], which is of little use as an integer, so allow promoting to double.
	if (bForceOpToDouble && PCG::Private::IsOfTypes<int32, int64>(InputTypeId))
	{
		return PCG::Private::MetadataTypes<double>::Id;
	}

	return InputTypeId;
}

#if WITH_EDITOR
FName UPCGMetadataStepOpSettings::GetDefaultNodeName() const
{
	switch (Operation)
	{
	case EPCGMetadataStepOperation::Step: return TEXT("Step");
	case EPCGMetadataStepOperation::MultiStep: return TEXT("MultiStep");
	default: return TEXT("SmoothStep");
	}
}

FText UPCGMetadataStepOpSettings::GetDefaultNodeTitle() const
{
	switch (Operation)
	{
	case EPCGMetadataStepOperation::Step: return LOCTEXT("StepNodeTitle", "Step");
	case EPCGMetadataStepOperation::MultiStep: return LOCTEXT("MultiStepNodeTitle", "Multi Step");
	default: return LOCTEXT("SmoothStepNodeTitle", "Smooth Step");
	}
}

FText UPCGMetadataStepOpSettings::GetNodeTooltipText() const
{
	if (Operation == EPCGMetadataStepOperation::Step)
	{
		return LOCTEXT("StepNodeTooltip", "Hard threshold, matching the HLSL step function.\n"
			"Result = (In >= Edge) ? 1 : 0.\n"
			"Vectors are computed per component.");
	}

	if (Operation == EPCGMetadataStepOperation::MultiStep)
	{
		return LOCTEXT("MultiStepNodeTooltip", "Staircase between Min and Max: like Smooth Step, but the result climbs in Step Count equal steps instead of easing.\n"
			"Alpha = saturate((In - Min) / (Max - Min)), Result = floor(Alpha * StepCount) / StepCount.\n"
			"With Step Count 5: 0, 0.2, 0.4, 0.6, 0.8 across the range, 1 at or above Max. Vectors are computed per component.");
	}

	return LOCTEXT("SmoothStepNodeTooltip", "Hermite interpolation between Min and Max, matching the material SmoothStep function.\n"
		"Alpha = saturate((In - Min) / (Max - Min)), Result = Alpha * Alpha * (3 - 2 * Alpha).\n"
		"Returns 0 at or below Min, 1 at or above Max, and a smoothly eased value in between. Vectors are computed per component.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGMetadataStepOpSettings::GetPreconfiguredInfo() const
{
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGMetadataStepOperation>();
}
#endif // WITH_EDITOR

void UPCGMetadataStepOpSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	if (const UEnum* EnumPtr = StaticEnum<EPCGMetadataStepOperation>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfiguredInfo.PreconfiguredIndex))
		{
			Operation = EPCGMetadataStepOperation(PreconfiguredInfo.PreconfiguredIndex);
		}
	}
}

FPCGElementPtr UPCGMetadataStepOpSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataStepOpElement>();
}

bool FPCGMetadataStepOpElement::DoOperation(PCGMetadataOps::FOperationData& OperationData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataStepOpElement::Execute);

	const UPCGMetadataStepOpSettings* Settings = CastChecked<UPCGMetadataStepOpSettings>(OperationData.Settings);

	auto StepFunc = [this, Operation = Settings->Operation, StepCount = FMath::Max(1, Settings->StepCount), &OperationData]<typename AttributeType>(AttributeType) -> bool
	{
		if constexpr (PCG::Private::IsOfTypes<AttributeType, float, double, int32, int64, FVector2D, FVector, FVector4>())
		{
			if (Operation == EPCGMetadataStepOperation::Step)
			{
				return DoBinaryOp<AttributeType, AttributeType>(OperationData,
					[](const AttributeType& Value, const AttributeType& Edge) -> AttributeType
					{
						return PCGMetadataStepOp::Step(Edge, Value);
					});
			}
			else if (Operation == EPCGMetadataStepOperation::MultiStep)
			{
				return DoTernaryOp<AttributeType, AttributeType, AttributeType>(OperationData,
					[StepCount](const AttributeType& Value, const AttributeType& Min, const AttributeType& Max) -> AttributeType
					{
						return PCGMetadataStepOp::MultiStep(Min, Max, Value, StepCount);
					});
			}
			else
			{
				return DoTernaryOp<AttributeType, AttributeType, AttributeType>(OperationData,
					[](const AttributeType& Value, const AttributeType& Min, const AttributeType& Max) -> AttributeType
					{
						return PCGMetadataStepOp::SmoothStep(Min, Max, Value);
					});
			}
		}
		else // Some other type not supported
		{
			ensure(false);
			return true;
		}
	};

	// If the output is double (integer inputs promoted), force all the operands to double.
	if (OperationData.OutputType == PCG::Private::MetadataTypes<double>::Id)
	{
		return StepFunc(double{});
	}
	else
	{
		return PCGMetadataAttribute::CallbackWithRightType(OperationData.MostComplexInputType, StepFunc);
	}
}

#undef LOCTEXT_NAMESPACE
