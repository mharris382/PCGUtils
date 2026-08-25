#include "Elements/PCGDynamicMeshSelectionProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionProcessBase"

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

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionProcessBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes(EPCGDataType::DynamicMesh);
	InputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynamicMeshSelectionProcessConstants::InputPin, MoveTemp(InputTypes), true, true).SetRequiredPin();
	Pins.Emplace(
		PCGDynamicMeshSelectionProcessConstants::SelectionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionProcessBaseSettings::OutputPinProperties() const
{
	FPCGDataTypeIdentifier OutputTypes(EPCGDataType::DynamicMesh);
	OutputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	return {FPCGPinProperties(
		PCGDynamicMeshSelectionProcessConstants::OutputPin, MoveTemp(OutputTypes), true, true)};
}

bool FPCGDynamicMeshSelectionProcessBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynamicMeshSelectionProcessBaseSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshSelectionProcessBaseSettings>();
	check(Settings);

	UE::Geometry::EGeometryElementType RequiredSelectionElementType =
		UE::Geometry::EGeometryElementType::Face;
	const bool bRequiresSelectionDomain =
		Settings->GetRequiredSelectionDomain(RequiredSelectionElementType);

	const TArray<FPCGTaggedData>& FactoryPinInputs = Context->InputData.GetInputsByPin(
		PCGDynamicMeshSelectionProcessConstants::SelectionFactoryInputPin);
	const UPCGUtilsDynMeshSelectionFactoryData* SelectionFactory = nullptr;
	if (!FactoryPinInputs.IsEmpty())
	{
		TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SelectionFactories;
		if (!PCGUtilsDynMeshFactories::GetInputFactories(
			Context, PCGDynamicMeshSelectionProcessConstants::SelectionFactoryInputPin,
			SelectionFactories, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), false))
		{
			return true;
		}
		if (SelectionFactories.Num() != 1)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("RequiresOneSelectionFactory", "Dynamic Mesh processor accepts at most one Selection Factory input."),
				Context);
			return true;
		}
		SelectionFactory = SelectionFactories[0];
	}

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGDynamicMeshSelectionProcessConstants::InputPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData
			? SelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "Dynamic Mesh processor skipped an input with no valid source mesh."), Context);
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
						LOCTEXT("SelectionDomainConversionFailed", "Dynamic Mesh processor could not convert its selection input to the domain required by the process."),
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
					LOCTEXT("SelectionFactoryEvaluationFailed", "Dynamic Mesh processor could not evaluate Selection Factory '{0}' in its effective selection domain."),
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
						LOCTEXT("CompleteSelectionOutputFailed", "Dynamic Mesh processor could not create its full-mesh selection output."),
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
		Output.Pin = PCGDynamicMeshSelectionProcessConstants::OutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
