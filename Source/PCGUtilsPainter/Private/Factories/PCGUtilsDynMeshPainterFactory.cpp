// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "Factories/PCGUtilsDynMeshFactories.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshPainterFactory"

PCG_DEFINE_TYPE_INFO(FPCGUtilsDynMeshPainterFactoryDataTypeInfo, UPCGUtilsDynMeshPainterFactoryData)

TSharedPtr<FPCGUtilsDynMeshPainterOperation> UPCGUtilsDynMeshPainterFactoryData::CreateOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = CreateOperationInternal();
	if (Operation)
	{
		Operation->BindContext(InContext);
	}
	return Operation;
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGUtilsDynMeshPainterFactoryData::CreateOperationInternal() const
{
	return nullptr;
}

bool FPCGUtilsDynMeshPainterOperation::Initialize(
	const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext)
{
	PainterContext = &InPainterContext;
	return true;
}

namespace PCGUtilsDynMeshFactories
{
	const TSet<FPCGDataTypeBaseId>& GetPainterFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {
			FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId()
		};
		return Types;
	}
}

EPCGUtilsDynMeshPainterColorChannel PCGUtilsDynMeshPainters::ResolveValueToColor(
	const FPCGUtilsDynMeshPainterValue& Value,
	EPCGUtilsDynMeshPainterColorChannel RequestedChannels,
	FVector4f& InOutColor)
{
	EPCGUtilsDynMeshPainterColorChannel WrittenChannels = RequestedChannels;
	if (Value.Type == EPCGUtilsDynMeshPainterValueType::Color)
	{
		WrittenChannels &= Value.ColorChannels;
	}

	const uint8 WrittenMask = static_cast<uint8>(WrittenChannels);
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		if ((WrittenMask & (1 << Channel)) != 0)
		{
			InOutColor[Channel] = Value.Type == EPCGUtilsDynMeshPainterValueType::Scalar
				? Value.Scalar : Value.Color[Channel];
		}
	}
	return WrittenChannels;
}

bool PCGUtilsDynMeshPainterFactories::GetSinglePainter(
	FPCGContext* Context,
	FName PinLabel,
	const UPCGUtilsDynMeshPainterFactoryData*& OutPainter,
	bool bRequired)
{
	check(Context);
	OutPainter = nullptr;
	if (Context->InputData.GetInputsByPin(PinLabel).IsEmpty())
	{
		if (bRequired)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingPainter", "Missing required Painter input on pin '{0}'."),
				FText::FromName(PinLabel)), Context);
		}
		return !bRequired;
	}

	TArray<TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData>> Painters;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		Context, PinLabel, Painters, PCGUtilsDynMeshFactories::GetPainterFactoryTypes(), false))
	{
		return false;
	}
	if (Painters.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("OnePainter", "Pin '{0}' accepts exactly one Painter."),
			FText::FromName(PinLabel)), Context);
		return false;
	}

	OutPainter = Painters[0];
	return true;
}

#undef LOCTEXT_NAMESPACE
