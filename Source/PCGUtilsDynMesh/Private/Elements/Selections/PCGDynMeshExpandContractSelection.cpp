// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshExpandContractSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExpandContractSelection"

namespace
{
	enum EPreconfiguredMode : int32
	{
		Expand = 0,
		Contract = 1
	};

	bool GetIndexType(
		UE::Geometry::EGeometryElementType ElementType,
		EGeometryScriptIndexType& OutIndexType)
	{
		switch (ElementType)
		{
		case UE::Geometry::EGeometryElementType::Vertex:
			OutIndexType = EGeometryScriptIndexType::Vertex;
			return true;
		case UE::Geometry::EGeometryElementType::Edge:
			OutIndexType = EGeometryScriptIndexType::Edge;
			return true;
		case UE::Geometry::EGeometryElementType::Face:
			OutIndexType = EGeometryScriptIndexType::Triangle;
			return true;
		default:
			return false;
		}
	}

	void AddElementToSelection(
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		int32 ElementID,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		switch (ElementType)
		{
		case EGeometryElementType::Vertex:
			if (Mesh.IsVertex(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(ElementID).Encoded());
			}
			break;
		case EGeometryElementType::Edge:
			if (Mesh.IsEdge(ElementID))
			{
				PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, ElementID, OutSelection);
			}
			break;
		case EGeometryElementType::Face:
			if (Mesh.IsTriangle(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(ElementID).Encoded());
			}
			break;
		default:
			break;
		}
	}
}

#if WITH_EDITOR
FText UPCGDynMeshExpandContractSelectionSettings::GetDefaultNodeTitle() const
{
	return bContract
		? LOCTEXT("ContractTitle", "Contract Selection")
		: LOCTEXT("ExpandTitle", "Expand Selection");
}

TArray<FText> UPCGDynMeshExpandContractSelectionSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("GrowAlias", "Grow Selection"),
		LOCTEXT("ShrinkAlias", "Shrink Selection")
	};
}

FText UPCGDynMeshExpandContractSelectionSettings::GetNodeTooltipText() const
{
	return bContract
		? LOCTEXT("ContractTooltip", "Shrinks an incoming DynMesh selection by removing connected boundary elements for each iteration.")
		: LOCTEXT("ExpandTooltip", "Grows an incoming DynMesh selection to connected neighbouring elements for each iteration.");
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGDynMeshExpandContractSelectionSettings::GetPreconfiguredInfo() const
{
	return {
		{EPreconfiguredMode::Expand,
			LOCTEXT("ExpandPreconfiguredTitle", "Expand Selection"),
			LOCTEXT("ExpandPreconfiguredTooltip", "Grows a DynMesh selection to connected neighbouring elements.")},
		{EPreconfiguredMode::Contract,
			LOCTEXT("ContractPreconfiguredTitle", "Contract Selection"),
			LOCTEXT("ContractPreconfiguredTooltip", "Shrinks a DynMesh selection by removing connected boundary elements.")}
	};
}

void UPCGDynMeshExpandContractSelectionSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case EPreconfiguredMode::Expand:
		bContract = false;
		break;
	case EPreconfiguredMode::Contract:
		bContract = true;
		break;
	default:
		ensureMsgf(false, TEXT("Unknown DynMesh Expand/Contract Selection preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshExpandContractSelectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshExpandContractSelectionConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshExpandContractSelectionSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(
		PCGDynMeshExpandContractSelectionConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

FPCGElementPtr UPCGDynMeshExpandContractSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshExpandContractSelectionElement>();
}

bool FPCGDynMeshExpandContractSelectionElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynMeshExpandContractSelectionSettings* Settings =
		Context->GetInputSettings<UPCGDynMeshExpandContractSelectionSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGDynMeshExpandContractSelectionConstants::SelectionPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!SelectionData || !DynamicMesh || !Mesh)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidSelection", "Expand/Contract Selection skipped an invalid selection or source mesh."),
				Context);
			continue;
		}

		const UE::Geometry::FGeometrySelection& IncomingSelection = SelectionData->GetSelection();
		EGeometryScriptIndexType IndexType = EGeometryScriptIndexType::Any;
		if (IncomingSelection.TopologyType != UE::Geometry::EGeometryTopologyType::Triangle ||
			!GetIndexType(IncomingSelection.ElementType, IndexType))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("UnsupportedSelection", "Expand/Contract Selection received an unsupported selection type."),
				Context);
			continue;
		}

		UE::Geometry::FGeometrySelection ResultSelection;
		if (IncomingSelection.IsEmpty() || Settings->Iterations <= 0)
		{
			ResultSelection = IncomingSelection;
		}
		else
		{
			FGeometryScriptMeshSelection ScriptSelection;
			ScriptSelection.SetSelection(IncomingSelection);
			FGeometryScriptMeshSelection ScriptResult;
			UGeometryScriptLibrary_MeshSelectionFunctions::ExpandContractMeshSelection(
				const_cast<UDynamicMesh*>(DynamicMesh), ScriptSelection, ScriptResult,
				Settings->Iterations, Settings->bContract, Settings->bOnlyExpandToFaceNeighbours);

			TArray<int32> ResultIDs;
			const EGeometryScriptIndexType ResultType =
				ScriptResult.ConvertToMeshIndexArray(*Mesh, ResultIDs, IndexType);
			if (ResultType != IndexType)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("SelectionConversionFailed", "Expand/Contract Selection could not preserve the incoming selection element type."),
					Context);
				continue;
			}

			ResultSelection.InitializeTypes(
				IncomingSelection.ElementType,
				UE::Geometry::EGeometryTopologyType::Triangle);
			for (const int32 ResultID : ResultIDs)
			{
				AddElementToSelection(*Mesh, IncomingSelection.ElementType, ResultID, ResultSelection);
			}
		}

		UPCGDynamicMeshSelectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(ResultSelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGDynMeshExpandContractSelectionConstants::SelectionPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
