// Copyright Max Harris

#include "Elements/Selections/PCGSelectionBoundaryEdges.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Elements/Selections/PCGDynMeshSelectionBoundaryFactory.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectionBoundaryEdges"

#if WITH_EDITOR
FText UPCGSelectionBoundaryEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select Boundary");
}

TArray<FText> UPCGSelectionBoundaryEdgesSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("BoundaryAlias", "Boundary of Selection"),
		LOCTEXT("OutlineAlias", "Selection Outline")
	};
}

FText UPCGSelectionBoundaryEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates the boundary of an incoming selection or selector. Materialized results are edge selections; selector results adapt to the domain requested downstream.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionBoundaryEdgesSettings::SelectorInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshSelectionBoundaryFactoryConstants::RegionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

bool UPCGSelectionBoundaryEdgesSettings::ProcessSelection(
	const UPCGDynamicMeshSelectionData* SelectionData,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
	const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!SelectionData || !DynamicMesh || !Mesh)
	{
		return false;
	}

	UE::Geometry::FGeometrySelection TriangleSelection;
	if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
		MeshData, *Mesh, SelectionData->GetSelection(),
		UE::Geometry::EGeometryElementType::Face, bAllowPartialInclusion, TriangleSelection))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("SelectionConversionFailed", "Select Boundary could not convert the incoming selection to triangles."), Context);
		return false;
	}

	OutSelection.InitializeTypes(
		UE::Geometry::EGeometryElementType::Edge, UE::Geometry::EGeometryTopologyType::Triangle);
	if (TriangleSelection.IsEmpty())
	{
		return true;
	}

	FGeometryScriptMeshSelection ScriptSelection;
	ScriptSelection.SetSelection(MoveTemp(TriangleSelection));
	FGeometryScriptMeshSelection ScriptBoundary;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectSelectionBoundaryEdges(
		const_cast<UDynamicMesh*>(DynamicMesh), ScriptSelection, ScriptBoundary, bExcludeMeshBoundaryEdges);

	TArray<int32> BoundaryEdgeIDs;
	if (ScriptBoundary.ConvertToMeshIndexArray(*Mesh, BoundaryEdgeIDs, EGeometryScriptIndexType::Edge) != EGeometryScriptIndexType::Edge)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("BoundaryConversionFailed", "Select Boundary could not convert the generated boundary to edges."), Context);
		return false;
	}

	for (const int32 EdgeID : BoundaryEdgeIDs)
	{
		if (Mesh->IsEdge(EdgeID))
		{
			PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(*Mesh, EdgeID, OutSelection);
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
