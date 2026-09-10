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
	const FName MaskPin = TEXT("Mask");

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
			if (Factory->Mask)
			{
				Mask = Factory->Mask->CreateOperation(Context);
				if (!Mask || !Mask->Initialize(InPainterContext) || Mask->GetOutputType() != EPCGUtilsDynMeshPainterValueType::Scalar)
				{
					PCGLog::LogErrorOnGraph(LOCTEXT("InvalidMask", "Painter Blend requires a scalar Mask Painter."), Context);
					return false;
				}
			}
			return true;
		}

		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			return A->Prepare(InPainterContext) && B->Prepare(InPainterContext)
				&& (!Mask || Mask->Prepare(InPainterContext));
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return A->GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color || B->GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color
				? EPCGUtilsDynMeshPainterValueType::Color : EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const float MaskValue = Mask ? Mask->Evaluate(Sample).Scalar : 1.0f;
			const float Weight = FMath::IsFinite(MaskValue) && FMath::IsFinite(Factory->Factor)
				? FMath::Clamp(Factory->Factor, 0.0f, 1.0f) * FMath::Clamp(MaskValue, 0.0f, 1.0f) : 0.0f;
			return PCGUtilsPainters::BlendValues(A->Evaluate(Sample), B->Evaluate(Sample), Factory->Operation, Weight);
		}

	private:
		TObjectPtr<const UPCGDynMeshPainterMathFactoryData> Factory;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> A;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> B;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Mask;
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
		float FactorValue = Factor;
		Ar << FactorValue;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshPainterMathProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter|Blend");
}

FString UPCGDynMeshPainterMathProviderSettings::GetAdditionalTitleInformation() const
{
	return StaticEnum<EPCGUtilsDynMeshPainterMathOperation>()->GetDisplayNameTextByValue(static_cast<int64>(Operation)).ToString();
}

FText UPCGDynMeshPainterMathProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Blends base A with blend B in linear value space. Factor interpolates from A to the blend result. Scalars broadcast to color channels; undefined blend channels preserve the base. Alpha is an ordinary channel, not implicit opacity. Results are not clamped.");
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
	Pins.Emplace(MaskPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshPainterMathProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	const UPCGUtilsDynMeshPainterFactoryData* APainter = nullptr;
	const UPCGUtilsDynMeshPainterFactoryData* BPainter = nullptr;
	const UPCGUtilsDynMeshPainterFactoryData* MaskPainter = nullptr;
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, APin, APainter, true) ||
		!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, BPin, BPainter, true) ||
		!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, MaskPin, MaskPainter, false))
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
	Factory->Factor = Factor;
	Factory->A = APainter;
	Factory->B = BPainter;
	Factory->Mask = MaskPainter;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
