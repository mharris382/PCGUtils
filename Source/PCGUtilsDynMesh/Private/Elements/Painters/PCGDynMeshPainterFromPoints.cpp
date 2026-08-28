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
			return FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) &&
				Factory && Factory->Radius > UE_SMALL_NUMBER;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const float RadiusSquared = FMath::Square(Factory->Radius);
			float Result = Factory->Reduction == EPCGUtilsDynMeshPainterPointReduction::Multiply
				? 1.0f : 0.0f;
			bool bHasContribution = false;

			for (const FPCGUtilsDynMeshPreparedPaintPoint& Point : Factory->Points)
			{
				const double DistanceSquared = FVector::DistSquared(Sample.WorldPosition, Point.WorldPosition);
				if (DistanceSquared > RadiusSquared)
				{
					continue;
				}

				const float NormalizedDistance =
					static_cast<float>(FMath::Sqrt(DistanceSquared)) / Factory->Radius;
				float Weight = 1.0f;
				switch (Factory->Falloff)
				{
				case EPCGUtilsDynMeshPainterFalloff::Linear:
					Weight = 1.0f - NormalizedDistance;
					break;
				case EPCGUtilsDynMeshPainterFalloff::Smooth:
					Weight = 1.0f - FMath::SmoothStep(0.0f, 1.0f, NormalizedDistance);
					break;
				case EPCGUtilsDynMeshPainterFalloff::Hard:
				default:
					break;
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
				Factory->bClampResult ? FMath::Clamp(Result, 0.0f, 1.0f) : Result);
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

	float RadiusValue = Radius;
	uint8 FalloffValue = static_cast<uint8>(Falloff);
	uint8 ReductionValue = static_cast<uint8>(Reduction);
	bool bClampValue = bClampResult;
	Ar << RadiusValue;
	Ar << FalloffValue;
	Ar << ReductionValue;
	Ar << bClampValue;
	ValueSelector.AddToCrc(Ar);
}

UPCGDynMeshPainterFromPointsProviderSettings::UPCGDynMeshPainterFromPointsProviderSettings()
{
	ValueSelector.SetPointProperty(EPCGPointProperties::Density);
}

#if WITH_EDITOR
FText UPCGDynMeshPainterFromPointsProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Paint from Points");
}

FText UPCGDynMeshPainterFromPointsProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable scalar Painter from spherical brushes centered on PCG points in world space.");
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
	if (Radius <= UE_SMALL_NUMBER)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidRadius", "Paint from Points requires Radius to be greater than zero."), InContext);
		return nullptr;
	}

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

		const FPCGAttributePropertyInputSelector FixedSelector = ValueSelector.CopyAndFixLast(PointData);
		TUniquePtr<const IPCGAttributeAccessor> Accessor =
			PCGAttributeAccessorHelpers::CreateConstAccessor(PointData, FixedSelector);
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys =
			PCGAttributeAccessorHelpers::CreateConstKeys(PointData, FixedSelector);
		if (!Accessor || !Keys)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingValue", "Paint from Points could not read scalar selector '{0}' from its point data."),
				FText::FromString(ValueSelector.ToString())), InContext);
			return nullptr;
		}

		const auto Transforms = PointData->GetConstTransformValueRange();
		PreparedPoints.Reserve(PreparedPoints.Num() + PointData->GetNumPoints());
		for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
		{
			float Value = 0.0f;
			if (!Transforms.IsValidIndex(PointIndex) ||
				!Accessor->Get<float>(Value, PointIndex, *Keys,
					EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("UnreadableValue", "Paint from Points could not read selector '{0}' for every input point."),
					FText::FromString(ValueSelector.ToString())), InContext);
				return nullptr;
			}

			FPCGUtilsDynMeshPreparedPaintPoint& Point = PreparedPoints.Emplace_GetRef();
			Point.WorldPosition = Transforms[PointIndex].GetLocation();
			Point.Value = Value;
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
	Factory->Radius = Radius;
	Factory->ValueSelector = ValueSelector;
	Factory->Falloff = Falloff;
	Factory->Reduction = Reduction;
	Factory->bClampResult = bClampResult;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
