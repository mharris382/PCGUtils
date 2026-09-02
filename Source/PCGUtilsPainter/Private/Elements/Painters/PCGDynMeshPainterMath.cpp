// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshPainterMath.h"

#include "Factories/PCGUtilsDynMeshFactories.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPainterMath"

namespace
{
	const FName APin = TEXT("A");
	const FName BPin = TEXT("B");

	class FPainterMathOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FPainterMathOperation(const UPCGDynMeshPainterMathFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory ||
				!Factory->A || !Factory->B)
			{
				return false;
			}

			A = Factory->A->CreateOperation(Context);
			B = Factory->B->CreateOperation(Context);
			if (!A || !B || !A->Initialize(InPainterContext) || !B->Initialize(InPainterContext))
			{
				return false;
			}
			if (A->GetOutputType() != EPCGUtilsDynMeshPainterValueType::Scalar ||
				B->GetOutputType() != EPCGUtilsDynMeshPainterValueType::Scalar)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ScalarOperandsRequired", "Painter Math currently requires scalar Painter operands."),
					Context);
				return false;
			}
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const float AValue = A->Evaluate(Sample).Scalar;
			const float BValue = B->Evaluate(Sample).Scalar;
			float Result = 0.0f;
			switch (Factory->Operation)
			{
			case EPCGUtilsDynMeshPainterMathOperation::Add:
				Result = AValue + BValue;
				break;
			case EPCGUtilsDynMeshPainterMathOperation::Subtract:
				Result = AValue - BValue;
				break;
			case EPCGUtilsDynMeshPainterMathOperation::Min:
				Result = FMath::Min(AValue, BValue);
				break;
			case EPCGUtilsDynMeshPainterMathOperation::Max:
				Result = FMath::Max(AValue, BValue);
				break;
			case EPCGUtilsDynMeshPainterMathOperation::Multiply:
			default:
				Result = AValue * BValue;
				break;
			}
			return FPCGUtilsDynMeshPainterValue::MakeScalar(Result);
		}

	private:
		TObjectPtr<const UPCGDynMeshPainterMathFactoryData> Factory;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> A;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> B;
	};

}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshPainterMathFactoryData::CreateOperationInternal() const
{
	return MakeShared<FPainterMathOperation>(this);
}

void UPCGDynMeshPainterMathFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 OperationValue = static_cast<uint8>(Operation);
		Ar << OperationValue;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshPainterMathProviderSettings::GetDefaultNodeTitle() const
{
	switch (Operation)
	{
	case EPCGUtilsDynMeshPainterMathOperation::Add: return LOCTEXT("AddTitle", "Painter +");
	case EPCGUtilsDynMeshPainterMathOperation::Subtract: return LOCTEXT("SubtractTitle", "Painter −");
	case EPCGUtilsDynMeshPainterMathOperation::Min: return LOCTEXT("MinTitle", "Painter Min");
	case EPCGUtilsDynMeshPainterMathOperation::Max: return LOCTEXT("MaxTitle", "Painter Max");
	case EPCGUtilsDynMeshPainterMathOperation::Multiply:
	default: return LOCTEXT("MultiplyTitle", "Painter ×");
	}
}

FText UPCGDynMeshPainterMathProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Combines two reusable Painter scalar fields directly at evaluation time.");
}
#endif

FName UPCGDynMeshPainterMathProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshPainterMathProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshPainterMathProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(APin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	Pins.Emplace_GetRef(BPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshPainterMathProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	const UPCGUtilsDynMeshPainterFactoryData* APainter = nullptr;
	const UPCGUtilsDynMeshPainterFactoryData* BPainter = nullptr;
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, APin, APainter, true) ||
		!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, BPin, BPainter, true))
	{
		return nullptr;
	}

	UPCGDynMeshPainterMathFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshPainterMathFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshPainterMathFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Operation = Operation;
	Factory->A = APainter;
	Factory->B = BPainter;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
