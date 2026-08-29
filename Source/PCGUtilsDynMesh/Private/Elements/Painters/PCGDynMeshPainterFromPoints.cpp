// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshPainterFromPoints.h"

#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPainterFromPoints"

namespace
{
	const FName PointsPin = TEXT("Points");

	class FPainterFromPointsOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FPainterFromPointsOperation(const UPCGDynMeshPainterFromPointsFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			return FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) && Factory;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			float Result = Factory->Reduction == EPCGUtilsDynMeshPainterPointReduction::Multiply
				? 1.0f : 0.0f;
			bool bHasContribution = false;

			for (const FPCGUtilsDynMeshPreparedPaintPoint& Point : Factory->Points)
			{
				float NormalizedDistance = 0.0f;
				if (Point.bUseBoundsShape)
				{
					const FVector LocalDelta =
						Point.WorldTransform.InverseTransformPosition(Sample.WorldPosition) -
						Point.LocalBoundsCenter;
					const FVector EllipsoidPosition(
						LocalDelta.X / Point.LocalOuterRadii.X,
						LocalDelta.Y / Point.LocalOuterRadii.Y,
						LocalDelta.Z / Point.LocalOuterRadii.Z);
					NormalizedDistance = static_cast<float>(EllipsoidPosition.Length());
				}
				else
				{
					NormalizedDistance = static_cast<float>(FVector::Distance(
						Sample.WorldPosition, Point.WorldTransform.GetLocation())) / Point.OuterRadius;
				}

				if (NormalizedDistance > 1.0f)
				{
					continue;
				}

				const float FalloffDistance = Point.InnerRadiusFraction >= 1.0f
					? 0.0f
					: FMath::Clamp(
						(NormalizedDistance - Point.InnerRadiusFraction) /
						(1.0f - Point.InnerRadiusFraction), 0.0f, 1.0f);
				float Weight = 1.0f;
				switch (Factory->Falloff)
				{
				case EPCGUtilsDynMeshPainterFalloff::Linear:
					Weight = 1.0f - FalloffDistance;
					break;
				case EPCGUtilsDynMeshPainterFalloff::Smooth:
					Weight = 1.0f - FMath::SmoothStep(0.0f, 1.0f, FalloffDistance);
					break;
				case EPCGUtilsDynMeshPainterFalloff::Hard:
				default:
					break;
				}
				if (Factory->Falloff != EPCGUtilsDynMeshPainterFalloff::Hard)
				{
					Weight = FMath::Pow(FMath::Clamp(Weight, 0.0f, 1.0f), Point.FalloffPower);
				}

				const float Contribution = Point.Value * Weight;
				if (!bHasContribution)
				{
					Result = Contribution;
					bHasContribution = true;
					continue;
				}

				switch (Factory->Reduction)
				{
				case EPCGUtilsDynMeshPainterPointReduction::Min:
					Result = FMath::Min(Result, Contribution);
					break;
				case EPCGUtilsDynMeshPainterPointReduction::Add:
					Result += Contribution;
					break;
				case EPCGUtilsDynMeshPainterPointReduction::Multiply:
					Result *= Contribution;
					break;
				case EPCGUtilsDynMeshPainterPointReduction::Max:
				default:
					Result = FMath::Max(Result, Contribution);
					break;
				}
			}

			if (!bHasContribution)
			{
				Result = 0.0f;
			}
			return FPCGUtilsDynMeshPainterValue::MakeScalar(
				Factory->bClampValue ? FMath::Clamp(Result, 0.0f, 1.0f) : Result);
		}

	private:
		TObjectPtr<const UPCGDynMeshPainterFromPointsFactoryData> Factory;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshPainterFromPointsFactoryData::CreateOperationInternal() const
{
	return MakeShared<FPainterFromPointsOperation>(this);
}

void UPCGDynMeshPainterFromPointsFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 RadiusSourceValue = static_cast<uint8>(RadiusSource);
	uint8 FalloffValue = static_cast<uint8>(Falloff);
	uint8 ReductionValue = static_cast<uint8>(Reduction);
	bool bInnerRadiusValue = bUseInnerRadius;
	bool bPowerAttributeValue = bUseFalloffPowerAttribute;
	float ConstantPowerValue = ConstantFalloffPower;
	bool bClampValueCrc = bClampValue;
	Ar << RadiusSourceValue;
	Ar << FalloffValue;
	Ar << ReductionValue;
	Ar << bInnerRadiusValue;
	Ar << bPowerAttributeValue;
	Ar << ConstantPowerValue;
	Ar << bClampValueCrc;
	ValueSelector.AddToCrc(Ar);
	RadiusSelector.AddToCrc(Ar);
	InnerRadiusSelector.AddToCrc(Ar);
	FalloffPowerSelector.AddToCrc(Ar);
}

UPCGDynMeshPainterFromPointsProviderSettings::UPCGDynMeshPainterFromPointsProviderSettings()
{
	ValueSelector.SetPointProperty(EPCGPointProperties::Density);
	RadiusSelector.SetAttributeName(TEXT("Radius"));
	InnerRadiusSelector.SetAttributeName(TEXT("InnerRadius"));
	FalloffPowerSelector.SetPointProperty(EPCGPointProperties::Steepness);
}

#if WITH_EDITOR
FText UPCGDynMeshPainterFromPointsProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Paint from Points");
}

FText UPCGDynMeshPainterFromPointsProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable scalar Painter from per-point brushes in world space. Outer influence comes from fitted point bounds (including oriented non-uniform ellipsoids) or a radius attribute; an optional inner radius creates a solid core before the powered falloff.");
}

FString UPCGDynMeshPainterFromPointsProviderSettings::GetAdditionalTitleInformation() const
{
	return ValueSelector.ToString();
}
#endif

FName UPCGDynMeshPainterFromPointsProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshPainterFromPointsProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshPainterFromPointsProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PointsPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshPainterFromPointsProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<FPCGUtilsDynMeshPreparedPaintPoint> PreparedPoints;
	bool bSawPointData = false;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PointsPin))
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data);
		if (!PointData)
		{
			continue;
		}
		bSawPointData = true;

		auto CreateReader = [PointData](const FPCGAttributePropertyInputSelector& Selector,
			TUniquePtr<const IPCGAttributeAccessor>& OutAccessor,
			TUniquePtr<const IPCGAttributeAccessorKeys>& OutKeys)
		{
			const FPCGAttributePropertyInputSelector FixedSelector = Selector.CopyAndFixLast(PointData);
			OutAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(PointData, FixedSelector);
			OutKeys = PCGAttributeAccessorHelpers::CreateConstKeys(PointData, FixedSelector);
			return OutAccessor.IsValid() && OutKeys.IsValid();
		};

		TUniquePtr<const IPCGAttributeAccessor> ValueAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> ValueKeys;
		if (!CreateReader(ValueSelector, ValueAccessor, ValueKeys))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingValue", "Paint from Points could not read scalar selector '{0}' from its point data."),
				FText::FromString(ValueSelector.ToString())), InContext);
			return nullptr;
		}

		TUniquePtr<const IPCGAttributeAccessor> RadiusAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> RadiusKeys;
		if (RadiusSource == EPCGUtilsDynMeshPainterRadiusSource::Attribute &&
			!CreateReader(RadiusSelector, RadiusAccessor, RadiusKeys))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingRadius", "Paint from Points could not read radius selector '{0}' from its point data."),
				FText::FromString(RadiusSelector.ToString())), InContext);
			return nullptr;
		}

		TUniquePtr<const IPCGAttributeAccessor> InnerRadiusAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> InnerRadiusKeys;
		if (bUseInnerRadius && !CreateReader(
			InnerRadiusSelector, InnerRadiusAccessor, InnerRadiusKeys))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingInnerRadius", "Paint from Points could not read inner-radius selector '{0}' from its point data."),
				FText::FromString(InnerRadiusSelector.ToString())), InContext);
			return nullptr;
		}

		TUniquePtr<const IPCGAttributeAccessor> PowerAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> PowerKeys;
		if (Falloff != EPCGUtilsDynMeshPainterFalloff::Hard && bUseFalloffPowerAttribute &&
			!CreateReader(FalloffPowerSelector, PowerAccessor, PowerKeys))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingFalloffPower", "Paint from Points could not read falloff-power selector '{0}' from its point data."),
				FText::FromString(FalloffPowerSelector.ToString())), InContext);
			return nullptr;
		}

		const auto Transforms = PointData->GetConstTransformValueRange();
		const auto BoundsMins = PointData->GetConstBoundsMinValueRange();
		const auto BoundsMaxs = PointData->GetConstBoundsMaxValueRange();
		PreparedPoints.Reserve(PreparedPoints.Num() + PointData->GetNumPoints());
		for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
		{
			float Value = 0.0f;
			if (!Transforms.IsValidIndex(PointIndex) ||
				!ValueAccessor->Get<float>(Value, PointIndex, *ValueKeys,
					EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("UnreadableValue", "Paint from Points could not read selector '{0}' for every input point."),
					FText::FromString(ValueSelector.ToString())), InContext);
				return nullptr;
			}

			FPCGUtilsDynMeshPreparedPaintPoint& Point = PreparedPoints.Emplace_GetRef();
			Point.WorldTransform = Transforms[PointIndex];
			Point.Value = Value;
			Point.bUseBoundsShape = RadiusSource == EPCGUtilsDynMeshPainterRadiusSource::Bounds;

			float OuterReferenceRadius = 0.0f;
			if (Point.bUseBoundsShape)
			{
				if (!BoundsMins.IsValidIndex(PointIndex) || !BoundsMaxs.IsValidIndex(PointIndex))
				{
					return nullptr;
				}
				Point.LocalBoundsCenter = (BoundsMins[PointIndex] + BoundsMaxs[PointIndex]) * 0.5;
				Point.LocalOuterRadii = (BoundsMaxs[PointIndex] - BoundsMins[PointIndex]).GetAbs() * 0.5;
				const FVector WorldRadii = Point.LocalOuterRadii * Point.WorldTransform.GetScale3D().GetAbs();
				OuterReferenceRadius = FMath::Max3(WorldRadii.X, WorldRadii.Y, WorldRadii.Z);
				if (Point.LocalOuterRadii.GetMin() <= UE_SMALL_NUMBER ||
					WorldRadii.GetMin() <= UE_SMALL_NUMBER)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("InvalidBoundsRadius", "Paint from Points requires non-zero fitted bounds on every point. Point {0} has a degenerate bound or scale."),
						FText::AsNumber(PointIndex)), InContext);
					return nullptr;
				}
			}
			else
			{
				if (!RadiusAccessor->Get<float>(Point.OuterRadius, PointIndex, *RadiusKeys,
					EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible) ||
					Point.OuterRadius <= UE_SMALL_NUMBER)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("InvalidAttributeRadius", "Paint from Points requires a positive radius for every point. Point {0} has an invalid radius."),
						FText::AsNumber(PointIndex)), InContext);
					return nullptr;
				}
				OuterReferenceRadius = Point.OuterRadius;
			}

			float InnerRadius = 0.0f;
			if (bUseInnerRadius && !InnerRadiusAccessor->Get<float>(
				InnerRadius, PointIndex, *InnerRadiusKeys,
				EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("InvalidInnerRadius", "Paint from Points could not read an inner radius for point {0}."),
					FText::AsNumber(PointIndex)), InContext);
				return nullptr;
			}
			Point.InnerRadiusFraction = FMath::Clamp(
				InnerRadius / OuterReferenceRadius, 0.0f, 1.0f);

			Point.FalloffPower = ConstantFalloffPower;
			if (Falloff != EPCGUtilsDynMeshPainterFalloff::Hard && bUseFalloffPowerAttribute &&
				!PowerAccessor->Get<float>(Point.FalloffPower, PointIndex, *PowerKeys,
					EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("InvalidFalloffPower", "Paint from Points could not read a falloff power for point {0}."),
					FText::AsNumber(PointIndex)), InContext);
				return nullptr;
			}
			Point.FalloffPower = FMath::Max(Point.FalloffPower, UE_SMALL_NUMBER);
		}
	}

	if (!bSawPointData)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingPoints", "Paint from Points requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshPainterFromPointsFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshPainterFromPointsFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshPainterFromPointsFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Points = MoveTemp(PreparedPoints);
	Factory->ValueSelector = ValueSelector;
	Factory->RadiusSource = RadiusSource;
	Factory->RadiusSelector = RadiusSelector;
	Factory->bUseInnerRadius = bUseInnerRadius;
	Factory->InnerRadiusSelector = InnerRadiusSelector;
	Factory->bUseFalloffPowerAttribute = bUseFalloffPowerAttribute;
	Factory->FalloffPowerSelector = FalloffPowerSelector;
	Factory->ConstantFalloffPower = ConstantFalloffPower;
	Factory->Falloff = Falloff;
	Factory->Reduction = Reduction;
	Factory->bClampValue = bClampValue;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
