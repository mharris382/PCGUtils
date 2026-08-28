#include "Elements/Topology/PCGBevelEdges.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGBevelEdges"

namespace
{
	const FName BevelMeshPin = TEXT("Mesh");
}

#if WITH_EDITOR
FText UPCGBevelEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Bevel Edges");
}

FText UPCGBevelEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Bevels a Dynamic Mesh edge selection (or every edge, if a bare Dynamic Mesh is supplied). Does not output a selection, since beveling changes mesh topology.");
}
#endif

FName UPCGBevelEdgesSettings::GetMainInputPinLabel() const
{
	return BevelMeshPin;
}

FPCGElementPtr UPCGBevelEdgesSettings::CreateElement() const
{
	return MakeShared<FPCGBevelEdgesElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGBevelEdgesSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshBevelEdgesOperation> Operation = MakeShared<FPCGUtilsDynMeshBevelEdgesOperation>();
	Operation->BevelDistance = BevelDistance;
	Operation->Subdivisions = Subdivisions;
	Operation->RoundWeight = RoundWeight;
	Operation->bInferMaterialID = bInferMaterialID;
	Operation->SetMaterialID = SetMaterialID;
	return Operation;
}

bool FPCGUtilsDynMeshBevelEdgesOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UDynamicMesh* TargetMesh = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
	if (!TargetMesh)
	{
		return false;
	}

	// Beveling adds geometry and renumbers elements, so nothing that referenced the old topology survives.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

	FGeometryScriptMeshSelection Selection;
	if (Invocation.SelectionData)
	{
		Selection.SetSelection(Invocation.SelectionData->GetSelection());
	}
	else
	{
		// Bevel always needs a selection, even for a bare Dynamic Mesh input - select every edge rather than
		// forcing the graph author to add an explicit "select all" step first.
		UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(
			TargetMesh, Selection, EGeometryScriptMeshSelectionType::Edges);
	}

	if (Selection.GetNumSelected() == 0)
	{
		// Legitimate no-op (an upstream filter found nothing this run); leave the mesh untouched.
		return true;
	}

	FGeometryScriptMeshBevelSelectionOptions BevelOptions;
	BevelOptions.BevelDistance = BevelDistance;
	BevelOptions.bInferMaterialID = bInferMaterialID;
	BevelOptions.SetMaterialID = SetMaterialID;
	BevelOptions.Subdivisions = Subdivisions;
	BevelOptions.RoundWeight = RoundWeight;

	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshBevelEdgeSelection(TargetMesh, Selection, BevelOptions);
	return true;
}

#undef LOCTEXT_NAMESPACE
