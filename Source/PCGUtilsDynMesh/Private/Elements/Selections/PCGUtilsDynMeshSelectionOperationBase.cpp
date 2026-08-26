// Copyright Max Harris

#include "Elements/Selections/PCGUtilsDynMeshSelectionOperationBase.h"

#include "Data/PCGDynamicMeshSelectionData.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGNode.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSelectionOperationBase"

void UPCGUtilsDynMeshSelectionOperationSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		InOutNode->RenameInputPin(TEXT("Seed Factory"), TEXT("Seed Selector"));
		InOutNode->RenameInputPin(TEXT("Region Factory"), TEXT("Region Selector"));
		InOutNode->RenameOutputPin(TEXT("Selection Factory"), TEXT("Selector"));
		InOutNode->RenameOutputPin(TEXT("Boundary"), PCGUtilsDynMeshSelectionOperationConstants::SelectionPin);
	}
}

FName UPCGUtilsDynMeshSelectionOperationSettings::GetMainOutputPin() const
{
	return OperationMode == EPCGUtilsDynMeshSelectionOperationMode::Selection
		? PCGUtilsDynMeshSelectionOperationConstants::SelectionPin
		: PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUtilsDynMeshSelectionOperationSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshSelectionOperationSettings::InputPinProperties() const
{
	if (OperationMode == EPCGUtilsDynMeshSelectionOperationMode::Selector)
	{
		return SelectorInputPinProperties();
	}

	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGUtilsDynMeshSelectionOperationConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshSelectionOperationSettings::OutputPinProperties() const
{
	if (OperationMode == EPCGUtilsDynMeshSelectionOperationMode::Selector)
	{
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGUtilsDynMeshSelectionOperationConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsDynMeshSelectionOperationSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsDynMeshSelectionOperationElement>();
}

bool FPCGUtilsDynMeshSelectionOperationElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsDynMeshSelectionOperationSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshSelectionOperationSettings>();
	check(Settings);

	if (Settings->OperationMode == EPCGUtilsDynMeshSelectionOperationMode::Selector)
	{
		UPCGUtilsDynMeshFactoryData* Selector = Settings->CreateFactory(Context);
		if (!Selector)
		{
			return true;
		}

		if (!Selector->Prepare(Context))
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("SelectorPreparationFailed", "Selector preparation failed."), Context);
			return true;
		}

		for (const FPCGPinProperties& InputPin : Settings->InputPinProperties())
		{
			for (const FPCGTaggedData& TaggedData : Context->InputData.GetInputsByPin(InputPin.Label))
			{
				Selector->AddDataDependency(TaggedData.Data);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = Selector;
		Output.Pin = Settings->GetMainOutputPin();
		return true;
	}

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGUtilsDynMeshSelectionOperationConstants::SelectionPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		if (!SelectionData || !SelectionData->GetSourceMeshData())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidSelection", "Selection operation skipped invalid selection data or a missing source mesh."), Context);
			continue;
		}

		UE::Geometry::FGeometrySelection ResultSelection;
		if (!Settings->ProcessSelection(SelectionData, Context, ResultSelection))
		{
			continue;
		}

		UPCGDynamicMeshSelectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(SelectionData->GetSourceMeshData(), MoveTemp(ResultSelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGUtilsDynMeshSelectionOperationConstants::SelectionPin;
	}

	return true;
}

void FPCGUtilsDynMeshSelectionOperationElement::DisabledPassThroughData(FPCGContext* Context) const
{
	if (!Context)
	{
		return;
	}
	const UPCGUtilsDynMeshSelectionOperationSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshSelectionOperationSettings>();
	if (Settings && Settings->OperationMode == EPCGUtilsDynMeshSelectionOperationMode::Selector)
	{
		Context->OutputData.TaggedData.Reset();
		return;
	}
	IPCGElement::DisabledPassThroughData(Context);
}

#undef LOCTEXT_NAMESPACE
