// Copyright Max Harris

#include "Elements/Selections/PCGSelectionBoundaryEdges.h"

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

#define LOCTEXT_NAMESPACE "PCGSelectionBoundaryEdges"

#if WITH_EDITOR
FText UPCGSelectionBoundaryEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Selection Boundary Edges");
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
	return LOCTEXT("Tooltip", "Creates an edge selection around the triangle-region boundary of an incoming DynMesh selection. Vertex and edge selections are converted through their incident triangles.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionBoundaryEdgesSettings::InputPinProperties() const
{
	return {FPCGPinProperties(
		PCGSelectionBoundaryEdgesConstants::SelectionInputPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

TArray<FPCGPinProperties> UPCGSelectionBoundaryEdgesSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(
		PCGSelectionBoundaryEdgesConstants::BoundaryOutputPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

FPCGElementPtr UPCGSelectionBoundaryEdgesSettings::CreateElement() const
{
	return MakeShared<FPCGSelectionBoundaryEdgesElement>();
}

bool FPCGSelectionBoundaryEdgesElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGSelectionBoundaryEdgesSettings* Settings =
		Context->GetInputSettings<UPCGSelectionBoundaryEdgesSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGSelectionBoundaryEdgesConstants::SelectionInputPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!SelectionData || !DynamicMesh || !Mesh)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidSelection", "Selection Boundary Edges skipped an invalid selection or source mesh."),
				Context);
			continue;
		}

		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(SelectionData->GetSelection());
		FGeometryScriptMeshSelection ScriptBoundary;
		UGeometryScriptLibrary_MeshSelectionFunctions::SelectSelectionBoundaryEdges(
			const_cast<UDynamicMesh*>(DynamicMesh), ScriptSelection, ScriptBoundary,
			Settings->bExcludeMeshBoundaryEdges);

		TArray<int32> BoundaryEdgeIDs;
		const EGeometryScriptIndexType ResultType = ScriptBoundary.ConvertToMeshIndexArray(
			*Mesh, BoundaryEdgeIDs, EGeometryScriptIndexType::Edge);
		if (ResultType != EGeometryScriptIndexType::Edge)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("BoundaryConversionFailed", "Selection Boundary Edges could not convert the generated boundary to edge IDs."),
				Context);
			continue;
		}

		UE::Geometry::FGeometrySelection BoundarySelection;
		BoundarySelection.InitializeTypes(
			UE::Geometry::EGeometryElementType::Edge,
			UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 EdgeID : BoundaryEdgeIDs)
		{
			if (Mesh->IsEdge(EdgeID))
			{
				PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(
					*Mesh, EdgeID, BoundarySelection);
			}
		}

		UPCGDynamicMeshSelectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(BoundarySelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGSelectionBoundaryEdgesConstants::BoundaryOutputPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
