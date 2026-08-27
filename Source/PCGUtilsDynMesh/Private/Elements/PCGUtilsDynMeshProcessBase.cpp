#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGNode.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshProcessBase"

void UPCGUtilsDynMeshProcessBaseSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		InOutNode->RenameInputPin(TEXT("Selection Factory"), PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin);
	}
}

namespace
{
	UE::Geometry::EGeometryElementType ToGeometryElementType(
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain Domain)
	{
		switch (Domain)
		{
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Vertex:
			return UE::Geometry::EGeometryElementType::Vertex;
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Edge:
			return UE::Geometry::EGeometryElementType::Edge;
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle:
		default:
			return UE::Geometry::EGeometryElementType::Face;
		}
	}

	bool BuildCompleteSelection(
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		OutSelection.InitializeTypes(ElementType, EGeometryTopologyType::Triangle);
		if (ElementType == EGeometryElementType::Vertex)
		{
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
			return true;
		}
		if (ElementType == EGeometryElementType::Edge)
		{
			for (const int32 EdgeID : Mesh.EdgeIndicesItr())
			{
				Mesh.EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&OutSelection](FMeshTriEdgeID TriEdgeID)
				{
					OutSelection.Selection.Add(FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
				});
			}
			return true;
		}
		if (ElementType == EGeometryElementType::Face)
		{
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
			return true;
		}
		return false;
	}
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshProcessBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes(EPCGDataType::DynamicMesh);
	InputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGUtilsDynMeshProcessConstants::InputPin, MoveTemp(InputTypes), true, true).SetRequiredPin();
	Pins.Emplace(
		PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshProcessBaseSettings::OutputPinProperties() const
{
	FPCGDataTypeIdentifier OutputTypes(EPCGDataType::DynamicMesh);
	OutputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	return {FPCGPinProperties(
		PCGUtilsDynMeshProcessConstants::OutputPin, MoveTemp(OutputTypes), true, true)};
}

const UPCGData* FPCGUtilsDynMeshResolvedInput::GetData() const
{
	return SelectionData ? static_cast<const UPCGData*>(SelectionData) : static_cast<const UPCGData*>(MeshData);
}

FPCGUtilsDynMeshResolvedInput FPCGUtilsDynMeshProcessFunctions::ResolveInput(
	const UPCGData* InputData, const UPCGUtilsDynMeshProcessBaseSettings* Settings, FPCGContext* Context)
{
	check(Settings);
	check(Context);

	FPCGUtilsDynMeshResolvedInput Result;
	const UPCGDynamicMeshSelectionData* InputSelection = Cast<const UPCGDynamicMeshSelectionData>(InputData);
	Result.MeshData = InputSelection ? InputSelection->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(InputData);
	const UDynamicMesh* SourceObject = Result.MeshData ? Result.MeshData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("ResolveInvalidInput", "DynMesh process skipped an input with no valid source mesh."), Context);
		Result.MeshData = nullptr;
		return Result;
	}

	const TArray<FPCGTaggedData>& FactoryPinInputs = Context->InputData.GetInputsByPin(
		PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin);
	const UPCGUtilsDynMeshSelectionFactoryData* SelectionFactory = nullptr;
	if (!FactoryPinInputs.IsEmpty())
	{
		TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SelectionFactories;
		if (!PCGUtilsDynMeshFactories::GetInputFactories(
			Context, PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin,
			SelectionFactories, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), false))
		{
			Result.MeshData = nullptr;
			return Result;
		}
		if (SelectionFactories.Num() != 1)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ResolveRequiresOneFactory", "DynMesh process accepts at most one Selector input."), Context);
			Result.MeshData = nullptr;
			return Result;
		}
		SelectionFactory = SelectionFactories[0];
	}

	if (Settings->RequiresSelection() && !InputSelection && !SelectionFactory)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ResolveSelectionRequired", "This DynMesh process requires either DynMesh Selection data or a connected Selector."),
			Context);
		Result.MeshData = nullptr;
		return Result;
	}

	UE::Geometry::EGeometryElementType RequiredElementType = UE::Geometry::EGeometryElementType::Face;
	const bool bRequiresDomain = Settings->GetRequiredSelectionDomain(RequiredElementType);
	if (!InputSelection && !SelectionFactory)
	{
		return Result;
	}

	const UE::Geometry::EGeometryElementType EffectiveElementType = bRequiresDomain
		? RequiredElementType
		: (SelectionFactory
			? ToGeometryElementType(Settings->SelectionFactoryEvaluationDomain)
			: InputSelection->GetSelection().ElementType);

	UE::Geometry::FGeometrySelection WorkingSelection;
	bool bHasSelection = false;
	if (InputSelection)
	{
		if (InputSelection->GetSelection().ElementType == EffectiveElementType)
		{
			WorkingSelection = InputSelection->GetSelection();
		}
		else if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
			Result.MeshData, *SourceMesh, InputSelection->GetSelection(), EffectiveElementType,
			Settings->AllowPartialSelectionDomainInclusion(), WorkingSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ResolveDomainConversionFailed", "DynMesh process could not convert its selection input to the required domain."),
				Context);
			Result.MeshData = nullptr;
			return Result;
		}
		bHasSelection = true;
	}

	if (SelectionFactory)
	{
		FPCGUtilsDynMeshSelectionDomain FactoryDomain;
		FactoryDomain.ElementType = EffectiveElementType;
		FactoryDomain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
		FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(Result.MeshData, *SourceMesh, FactoryDomain);
		UE::Geometry::FGeometrySelection FactorySelection;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
			SelectionFactory, EvaluationContext, Context, FactorySelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ResolveFactoryEvaluationFailed", "DynMesh process could not evaluate its Selector."), Context);
			Result.MeshData = nullptr;
			return Result;
		}
		if (bHasSelection)
		{
			WorkingSelection.Selection = WorkingSelection.Selection.Intersect(FactorySelection.Selection);
		}
		else
		{
			WorkingSelection = MoveTemp(FactorySelection);
			bHasSelection = true;
		}
	}

	if (bHasSelection)
	{
		UPCGDynamicMeshSelectionData* ResolvedSelection =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		ResolvedSelection->Initialize(Result.MeshData, MoveTemp(WorkingSelection));
		Result.SelectionData = ResolvedSelection;
	}
	return Result;
}

bool FPCGUtilsDynMeshProcessBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsDynMeshProcessBaseSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshProcessBaseSettings>();
	check(Settings);

	UE::Geometry::EGeometryElementType RequiredSelectionElementType =
		UE::Geometry::EGeometryElementType::Face;
	const bool bRequiresSelectionDomain =
		Settings->GetRequiredSelectionDomain(RequiredSelectionElementType);

	const TArray<FPCGTaggedData>& FactoryPinInputs = Context->InputData.GetInputsByPin(
		PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin);
	const UPCGUtilsDynMeshSelectionFactoryData* SelectionFactory = nullptr;
	if (!FactoryPinInputs.IsEmpty())
	{
		TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SelectionFactories;
		if (!PCGUtilsDynMeshFactories::GetInputFactories(
			Context, PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin,
			SelectionFactories, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), false))
		{
			return true;
		}
		if (SelectionFactories.Num() != 1)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("RequiresOneSelectionFactory", "DynMesh processor accepts at most one Selector input."),
				Context);
			return true;
		}
		SelectionFactory = SelectionFactories[0];
	}

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGUtilsDynMeshProcessConstants::InputPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData
			? SelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "DynMesh processor skipped an input with no valid source mesh."), Context);
			continue;
		}
		if (Settings->RequiresSelection() && !SelectionData && !SelectionFactory)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("SelectionRequired", "This DynMesh process requires either DynMesh Selection data or a connected Selector."),
				Context);
			continue;
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials())
		{
			Materials.Add(Material);
		}

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(UE::Geometry::FDynamicMesh3(*SourceMesh), MoveTemp(Materials));

		const UE::Geometry::EGeometryElementType EffectiveSelectionElementType =
			bRequiresSelectionDomain
				? RequiredSelectionElementType
				: (SelectionFactory
					? ToGeometryElementType(Settings->SelectionFactoryEvaluationDomain)
					: (SelectionData
						? SelectionData->GetSelection().ElementType
						: UE::Geometry::EGeometryElementType::Face));

		UE::Geometry::FGeometrySelection WorkingSelection;
		bool bHasWorkingSelection = false;
		bool bSelectionWasConverted = false;
		if (SelectionData)
		{
			if (SelectionData->GetSelection().ElementType == EffectiveSelectionElementType)
			{
				WorkingSelection = SelectionData->GetSelection();
			}
			else
			{
				const UDynamicMesh* OutputObject = OutputData->GetDynamicMesh();
				const UE::Geometry::FDynamicMesh3* OutputMesh =
					OutputObject ? OutputObject->GetMeshPtr() : nullptr;
				if (!OutputMesh || !PCGUtilsDynMeshSelectionDomains::ConvertSelection(
					OutputData, *OutputMesh, SelectionData->GetSelection(),
					EffectiveSelectionElementType,
					Settings->AllowPartialSelectionDomainInclusion(), WorkingSelection))
				{
					PCGLog::LogErrorOnGraph(
						LOCTEXT("SelectionDomainConversionFailed", "DynMesh processor could not convert its selection input to the domain required by the process."),
						Context);
					continue;
				}
				bSelectionWasConverted = true;
			}
			bHasWorkingSelection = true;
		}

		if (SelectionFactory)
		{
			const UDynamicMesh* OutputObject = OutputData->GetDynamicMesh();
			const UE::Geometry::FDynamicMesh3* OutputMesh =
				OutputObject ? OutputObject->GetMeshPtr() : nullptr;
			if (!OutputMesh)
			{
				continue;
			}

			FPCGUtilsDynMeshSelectionDomain FactoryDomain;
			FactoryDomain.ElementType = EffectiveSelectionElementType;
			FactoryDomain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
			FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(
				OutputData, *OutputMesh, FactoryDomain);
			UE::Geometry::FGeometrySelection FactorySelection;
			if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
				SelectionFactory, EvaluationContext, Context, FactorySelection))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("SelectionFactoryEvaluationFailed", "DynMesh processor could not evaluate Selector '{0}' in its effective selection domain."),
					FText::FromString(SelectionFactory->GetClass()->GetName())), Context);
				continue;
			}

			if (bHasWorkingSelection)
			{
				WorkingSelection.Selection =
					WorkingSelection.Selection.Intersect(FactorySelection.Selection);
			}
			else
			{
				WorkingSelection = MoveTemp(FactorySelection);
				bHasWorkingSelection = true;
			}
		}

		const UPCGDynamicMeshSelectionData* ProcessSelectionData = SelectionData;
		if (bHasWorkingSelection && (SelectionFactory || bSelectionWasConverted))
		{
			UPCGDynamicMeshSelectionData* ConvertedSelectionData =
				FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
			ConvertedSelectionData->Initialize(OutputData, MoveTemp(WorkingSelection));
			ProcessSelectionData = ConvertedSelectionData;
		}

		if (!ProcessMesh(OutputData, ProcessSelectionData, Context))
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		if (Settings->bOutputSelectionData)
		{
			UE::Geometry::FGeometrySelection OutputSelection;
			if (ProcessSelectionData)
			{
				OutputSelection = ProcessSelectionData->GetSelection();
			}
			else
			{
				const UDynamicMesh* ProcessedObject = OutputData->GetDynamicMesh();
				const UE::Geometry::FDynamicMesh3* ProcessedMesh =
					ProcessedObject ? ProcessedObject->GetMeshPtr() : nullptr;
				const UE::Geometry::EGeometryElementType OutputElementType =
					bRequiresSelectionDomain
						? RequiredSelectionElementType
						: ToGeometryElementType(Settings->SelectionFactoryEvaluationDomain);
				if (!ProcessedMesh || !BuildCompleteSelection(
					*ProcessedMesh, OutputElementType, OutputSelection))
				{
					PCGLog::LogErrorOnGraph(
						LOCTEXT("CompleteSelectionOutputFailed", "DynMesh processor could not create its full-mesh selection output."),
						Context);
					continue;
				}
			}
			UPCGDynamicMeshSelectionData* OutputSelectionData =
				FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
			OutputSelectionData->Initialize(OutputData, MoveTemp(OutputSelection));
			Output.Data = OutputSelectionData;
		}
		else
		{
			Output.Data = OutputData;
		}
		Output.Pin = PCGUtilsDynMeshProcessConstants::OutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
