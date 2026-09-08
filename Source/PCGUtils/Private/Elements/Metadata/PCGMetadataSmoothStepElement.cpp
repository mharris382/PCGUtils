// Copyright Max Harris

#include "Elements/Metadata/PCGMetadataSmoothStepElement.h"

#include "PCGPin.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMetadataSmoothStepElement)

#define LOCTEXT_NAMESPACE "PCGMetadataSmoothStepElement"

namespace PCGMetadataSmoothStep
{
	/** Scalar SmoothStep, computed in double precision then cast back to the attribute type. */
	inline double SmoothStepScalar(const double Min, const double Max, const double Value)
	{
		const double Range = Max - Min;

		// Degenerate range: the transition has no width, so this becomes a hard step, mirroring HLSL smoothstep's
		// behaviour for Value outside of [Min, Max].
		const double Alpha = FMath::IsNearlyZero(Range) ? ((Value >= Max) ? 1.0 : 0.0) : FMath::Clamp((Value - Min) / Range, 0.0, 1.0);

		return Alpha * Alpha * (3.0 - 2.0 * Alpha);
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
}

FName UPCGMetadataSmoothStepSettings::GetInputPinLabel(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return PCGPinConstants::DefaultInputLabel;
	case 1:
		return PCGMetadataSettingsBaseConstants::ClampMinLabel;
	case 2:
		return PCGMetadataSettingsBaseConstants::ClampMaxLabel;
	default:
		return NAME_None;
	}
}

FPCGAttributePropertyInputSelector UPCGMetadataSmoothStepSettings::GetInputSource(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return InputSource1;
	case 1:
		return InputSource2;
	case 2:
		return InputSource3;
	default:
		return FPCGAttributePropertyInputSelector();
	}
}

bool UPCGMetadataSmoothStepSettings::IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const
{
	bHasSpecialRequirement = false;
	return PCG::Private::IsOfTypes<float, double, int32, int64, FVector2D, FVector, FVector4>(TypeId);
}

uint16 UPCGMetadataSmoothStepSettings::GetOutputType(uint16 InputTypeId) const
{
	// SmoothStep always returns a value in [0, 1], which is meaningless as an integer, so allow promoting to double.
	if (bForceOpToDouble && PCG::Private::IsOfTypes<int32, int64>(InputTypeId))
	{
		return PCG::Private::MetadataTypes<double>::Id;
	}

	return InputTypeId;
}

#if WITH_EDITOR
FName UPCGMetadataSmoothStepSettings::GetDefaultNodeName() const
{
	return TEXT("AttributeSmoothStep");
}

FText UPCGMetadataSmoothStepSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Attribute Smooth Step");
}

FText UPCGMetadataSmoothStepSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Hermite interpolation between Min and Max, matching the material SmoothStep function.\n"
		"Alpha = saturate((In - Min) / (Max - Min)), Result = Alpha * Alpha * (3 - 2 * Alpha).\n"
		"Returns 0 at or below Min, 1 at or above Max, and a smoothly eased value in between. Vectors are computed per component.");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGMetadataSmoothStepSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataSmoothStepElement>();
}

bool FPCGMetadataSmoothStepElement::DoOperation(PCGMetadataOps::FOperationData& OperationData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataSmoothStepElement::Execute);

	auto SmoothStepFunc = [this, &OperationData]<typename AttributeType>(AttributeType) -> bool
	{
		if constexpr (PCG::Private::IsOfTypes<AttributeType, float, double, int32, int64, FVector2D, FVector, FVector4>())
		{
			return DoTernaryOp<AttributeType, AttributeType, AttributeType>(OperationData,
				[](const AttributeType& Value, const AttributeType& Min, const AttributeType& Max) -> AttributeType
				{
					return PCGMetadataSmoothStep::SmoothStep(Min, Max, Value);
				});
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
		return SmoothStepFunc(double{});
	}
	else
	{
		return PCGMetadataAttribute::CallbackWithRightType(OperationData.MostComplexInputType, SmoothStepFunc);
	}
}

#undef LOCTEXT_NAMESPACE
