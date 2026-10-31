// Copyright Max Harris

#include "Elements/Painters/PCGUtilsPainterDecorator.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPainterDecorator"

FPCGUtilsPainterDecoratorOperation::FPCGUtilsPainterDecoratorOperation(
	const UPCGUtilsDynMeshPainterDecoratorFactoryData* InFactory)
	: DecoratorFactory(InFactory)
{
}

bool FPCGUtilsPainterDecoratorOperation::Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext)
{
	if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !DecoratorFactory)
	{
		return false;
	}
	if (!DecoratorFactory->Source)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoSource",
			"A Painter decorator has no Painter connected to its 'Painter' input."), Context);
		return false;
	}

	SourceOperation = DecoratorFactory->Source->CreateOperation(Context);
	return SourceOperation && SourceOperation->Initialize(InPainterContext);
}

bool FPCGUtilsPainterDecoratorOperation::Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext)
{
	return SourceOperation && SourceOperation->Prepare(InPainterContext);
}

EPCGUtilsDynMeshPainterValueType FPCGUtilsPainterDecoratorOperation::GetOutputType() const
{
	return SourceOperation ? SourceOperation->GetOutputType() : EPCGUtilsDynMeshPainterValueType::Scalar;
}

FName UPCGUtilsDynMeshPainterDecoratorProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUtilsDynMeshPainterDecoratorProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshPainterDecoratorProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGUtilsPainterDecoratorConstants::SourcePin,
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGUtilsDynMeshPainterDecoratorProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGUtilsDynMeshPainterDecoratorFactoryData* Factory = Cast<UPCGUtilsDynMeshPainterDecoratorFactoryData>(InFactory);
	if (!Factory)
	{
		return nullptr;
	}

	const UPCGUtilsDynMeshPainterFactoryData* SourcePainter = nullptr;
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(
		InContext, PCGUtilsPainterDecoratorConstants::SourcePin, SourcePainter, /*bRequired=*/true))
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Source = SourcePainter;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
