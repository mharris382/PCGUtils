// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"

#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshAxisGradientPainter"

namespace
{
	class FAxisGradientPainterOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FAxisGradientPainterOperation(const UPCGDynMeshAxisGradientPainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			return FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) && Factory &&
				!Factory->Axis.IsNearlyZero() &&
				!FMath::IsNearlyEqual(Factory->StartDistance, Factory->EndDistance);
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const FVector& Position =
				Factory->CoordinateSpace == EPCGUtilsDynMeshPainterCoordinateSpace::World
					? Sample.WorldPosition : Sample.LocalPosition;
			const double Projection = FVector::DotProduct(Position - Factory->Origin, Factory->Axis);
			float Value = FMath::Clamp(static_cast<float>(
				(Projection - Factory->StartDistance) /
				(Factory->EndDistance - Factory->StartDistance)), 0.0f, 1.0f);
			return FPCGUtilsDynMeshPainterValue::MakeScalar(
				Factory->bInvert ? 1.0f - Value : Value);
		}

	private:
		TObjectPtr<const UPCGDynMeshAxisGradientPainterFactoryData> Factory;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshAxisGradientPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FAxisGradientPainterOperation>(this);
}

void UPCGDynMeshAxisGradientPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	FVector OriginValue = Origin;
	FVector AxisValue = Axis;
	float StartValue = StartDistance;
	float EndValue = EndDistance;
	bool bInvertValue = bInvert;
	uint8 SpaceValue = static_cast<uint8>(CoordinateSpace);
	Ar << OriginValue;
	Ar << AxisValue;
	Ar << StartValue;
	Ar << EndValue;
	Ar << bInvertValue;
	Ar << SpaceValue;
}

#if WITH_EDITOR
FText UPCGDynMeshAxisGradientPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Axis Gradient Painter");
}

FText UPCGDynMeshAxisGradientPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable linear scalar gradient projected along an axis in DynMesh-local or world space.");
}
#endif

FName UPCGDynMeshAxisGradientPainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshAxisGradientPainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshAxisGradientPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	if (Axis.IsNearlyZero())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ZeroAxis", "Axis Gradient Painter requires a non-zero Axis."), InContext);
		return nullptr;
	}
	if (FMath::IsNearlyEqual(StartDistance, EndDistance))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidRange", "Axis Gradient Painter requires different Start and End distances."),
			InContext);
		return nullptr;
	}

	UPCGDynMeshAxisGradientPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshAxisGradientPainterFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshAxisGradientPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Origin = Origin;
	Factory->Axis = Axis.GetSafeNormal();
	Factory->StartDistance = StartDistance;
	Factory->EndDistance = EndDistance;
	Factory->bInvert = bInvert;
	Factory->CoordinateSpace = CoordinateSpace;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
