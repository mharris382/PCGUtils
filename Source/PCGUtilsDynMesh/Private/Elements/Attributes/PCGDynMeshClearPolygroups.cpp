// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshClearPolygroups.h"

#include "GeometryScript/MeshPolygroupFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetHandle.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshClearPolygroups"

#if WITH_EDITOR
FText UPCGDynMeshClearPolygroupsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Clear PolyGroups");
}

FText UPCGDynMeshClearPolygroupsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Resets the PolyGroup IDs of a Dynamic Mesh triangle selection to a clear value. The selected region is "
		"extracted, cleared, and welded back, leaving unselected triangles' PolyGroups untouched. With no "
		"selection the whole layer is cleared.");
}
#endif

bool UPCGDynMeshClearPolygroupsSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

FPCGElementPtr UPCGDynMeshClearPolygroupsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshClearPolygroupsElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshClearPolygroupsSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshClearPolygroupsOperation> Operation =
		MakeShared<FPCGUtilsDynMeshClearPolygroupsOperation>();
	Operation->GroupLayer = GroupLayer;
	Operation->ClearValue = ClearValue;
	return Operation;
}

bool FPCGUtilsDynMeshClearPolygroupsOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	// Only the PolyGroup attribute changes; the triangulation is untouched.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	// ClearPolygroups clears the whole layer of whatever mesh it is handed, so restrict it to the selection by
	// extracting the selected triangles into a region and welding the cleared region back afterwards. With no
	// selection the handle hands over the whole mesh and the whole layer is cleared.
	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::Region);
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!Handle.IsEmptySelectionNoOp())
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::ClearPolygroups(
			Handle.GetTargetMesh(), GroupLayer, ClearValue);
	}

	FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);

	// CreateTargetInPlace adopted Invocation.MeshData's mesh, so RestoreRegion put the result there directly.
	return true;
}

#undef LOCTEXT_NAMESPACE
