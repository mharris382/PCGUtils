// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshSelfUnion.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSelfUnion"

#if WITH_EDITOR
FText UPCGDynMeshSelfUnionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Self Union");
}

FText UPCGDynMeshSelfUnionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Unions each mesh with itself: parts that pass through each other become one solid and the faces left "
		"inside it are removed. Use it on a mesh merged from overlapping pieces before a Boolean or a cut. Whole "
		"mesh only: selection data on the input is refused.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshSelfUnionSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshSelfUnionOperation> Operation = MakeShared<FPCGUtilsDynMeshSelfUnionOperation>();
	Operation->Options = Options;
	return Operation;
}

FPCGElementPtr UPCGDynMeshSelfUnionSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSelfUnionElement>();
}

bool FPCGUtilsDynMeshSelfUnionOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	check(Invocation.MeshData);

	// Inside and outside are decided from the whole mesh, so a part of it cannot be unioned on its own.
	if (Invocation.SelectionData)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("SelectionRefused",
			"DynMesh | Self Union works on a whole mesh and was given a selection; that input was skipped. Connect the mesh itself."),
			Invocation.Context);
		return false;
	}

	UDynamicMesh* Mesh = Invocation.MeshData->GetMutableDynamicMesh();
	if (!Mesh)
	{
		return false;
	}

	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Mesh, Options);
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
	return true;
}

#undef LOCTEXT_NAMESPACE
