// Copyright Max Harris

#include "Elements/Selections/PCGUtilsDynMeshSelectionOperationBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGNode.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSelectionOperationBase"

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsDynMeshSelectionOperationSettings::MakeRepresentationPresets(
	const FText& DisplayName, int32 SelectorIndex, int32 SelectionIndex)
{
	return {
		{SelectorIndex,
			FText::Format(LOCTEXT("SelectorPresetTitle", "{0} (Selector)"), DisplayName),
			LOCTEXT("SelectorPresetTooltip", "Creates the deferred, composable Selector representation."),
			LOCTEXT("SelectorPresetSearchHints", "selector deferred factory")},
		{SelectionIndex,
			FText::Format(LOCTEXT("SelectionPresetTitle", "{0} (Selection)"), DisplayName),
			LOCTEXT("SelectionPresetTooltip", "Evaluates immediately and outputs a mesh-bound Selection data stream."),
			LOCTEXT("SelectionPresetSearchHints", "selection materialized inline live data")}
	};
}

bool UPCGUtilsDynMeshSelectionOperationSettings::ApplyRepresentationPreset(
	int32 PreconfiguredIndex, int32 SelectorIndex, int32 SelectionIndex,
	EPCGUtilsDynMeshSelectionOperationMode& OutMode)
{
	if (PreconfiguredIndex == SelectorIndex)
	{
		OutMode = EPCGUtilsDynMeshSelectionOperationMode::Selector;
		return true;
	}
	if (PreconfiguredIndex == SelectionIndex)
	{
		OutMode = EPCGUtilsDynMeshSelectionOperationMode::Selection;
		return true;
	}
	return false;
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsDynMeshSelectionOperationSettings::GetPreconfiguredInfo() const
{
	return MakeRepresentationPresets(GetDefaultNodeTitle(),
		PCGUtilsDynMeshSelectionOperationConstants::SelectorPreconfiguredIndex,
		PCGUtilsDynMeshSelectionOperationConstants::SelectionPreconfiguredIndex);
}

void UPCGUtilsDynMeshSelectionOperationSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	ApplyRepresentationPreset(PreconfiguredInfo.PreconfiguredIndex,
		PCGUtilsDynMeshSelectionOperationConstants::SelectorPreconfiguredIndex,
		PCGUtilsDynMeshSelectionOperationConstants::SelectionPreconfiguredIndex, OperationMode);
}
#endif

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

UPCGUtilsDynMeshFactoryData* UPCGUtilsDynMeshSelectionOperationSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	if (InFactory)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("UnexpectedFactoryOverride", "Selection decorators do not accept a preallocated factory."), InContext);
		return nullptr;
	}

	const TArray<FPCGPinProperties> SelectorPins = SelectorInputPinProperties();
	if (SelectorPins.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidDecoratorPinContract", "Selection decorators must declare exactly one child Selector pin."), InContext);
		return nullptr;
	}

	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> ChildSelectors;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, SelectorPins[0].Label, ChildSelectors,
		PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return nullptr;
	}
	if (ChildSelectors.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneChildSelector", "Selection decorators require exactly one child Selector."), InContext);
		return nullptr;
	}

	return CreateDecoratorFactory(InContext, ChildSelectors[0]);
}

UE::Geometry::EGeometryElementType
UPCGUtilsDynMeshSelectionOperationSettings::GetMaterializedOutputElementType(
	const UPCGDynamicMeshSelectionData* SelectionData) const
{
	return SelectionData
		? SelectionData->GetSelection().ElementType
		: UE::Geometry::EGeometryElementType::Face;
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

		const UPCGDynamicMeshData* MeshData = SelectionData->GetSourceMeshData();
		const UDynamicMesh* DynamicMesh = MeshData->GetDynamicMesh();
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			continue;
		}

		UPCGUtilsDynMeshLiteralSelectionFactoryData* LiteralSelector =
			FPCGContext::NewObject_AnyThread<UPCGUtilsDynMeshLiteralSelectionFactoryData>(Context);
		LiteralSelector->SelectionData = SelectionData;
		LiteralSelector->bAllowPartialInclusion = Settings->bAllowPartialInclusion;

		UPCGUtilsDynMeshSelectionFactoryData* Decorator =
			Settings->CreateDecoratorFactory(Context, LiteralSelector);
		if (!Decorator || !Decorator->Prepare(Context))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("DecoratorPreparationFailed", "Selection decorator preparation failed."), Context);
			continue;
		}

		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = Settings->GetMaterializedOutputElementType(SelectionData);
		Domain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
		FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(MeshData, *Mesh, Domain);
		UE::Geometry::FGeometrySelection ResultSelection;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
			Decorator, EvaluationContext, Context, ResultSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("DecoratorEvaluationFailed", "Selection decorator evaluation failed."), Context);
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
