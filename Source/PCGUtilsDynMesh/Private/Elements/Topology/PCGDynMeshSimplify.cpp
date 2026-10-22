// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshSimplify.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSimplify"

#if WITH_EDITOR
FText UPCGDynMeshSimplifySettings::GetDefaultNodeTitle() const
{
	return FText::Format(LOCTEXT("Title", "DynMesh | Simplify {0}"), PCGUtilsDynMeshSimplify::ModeText(Mode));
}

FText UPCGDynMeshSimplifySettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Exposes Geometry Script's planar, polygroup, count, tolerance, and edge-length simplification. A DynMesh "
		"Selection or Selector simplifies only the selected region and restores it into the untouched mesh.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation>
UPCGDynMeshSimplifySettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshSimplifyOperation> Operation = MakeShared<FPCGUtilsDynMeshSimplifyOperation>();
	Operation->Mode = Mode;
	Operation->PlanarOptions = PlanarOptions;
	Operation->PolygroupOptions = PolygroupOptions;
	Operation->GroupLayer = GroupLayer;
	Operation->TargetTriangleCount = TargetTriangleCount;
	Operation->TargetVertexCount = TargetVertexCount;
	Operation->GeometricTolerance = GeometricTolerance;
	Operation->TargetEdgeLength = TargetEdgeLength;
	Operation->SimplifyOptions = SimplifyOptions;
	Operation->ClusterOptions = ClusterOptions;
	return Operation;
}

FPCGElementPtr UPCGDynMeshSimplifySettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSimplifyElement>();
}

FPCGDynMeshSimplifyParameters FPCGUtilsDynMeshSimplifyOperation::ToParameters() const
{
	FPCGDynMeshSimplifyParameters Parameters;
	Parameters.Mode = Mode;
	Parameters.PlanarOptions = PlanarOptions;
	Parameters.PolygroupOptions = PolygroupOptions;
	Parameters.GroupLayer = GroupLayer;
	Parameters.TargetTriangleCount = TargetTriangleCount;
	Parameters.TargetVertexCount = TargetVertexCount;
	Parameters.GeometricTolerance = GeometricTolerance;
	Parameters.TargetEdgeLength = TargetEdgeLength;
	Parameters.SimplifyOptions = SimplifyOptions;
	Parameters.ClusterOptions = ClusterOptions;
	return Parameters;
}

bool FPCGUtilsDynMeshSimplifyOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::Region);
	if (!Handle.IsValid())
	{
		return false;
	}

	const bool bSelectionRegion = Handle.IsSelection() && !Handle.IsEmptySelectionNoOp();
	const FPCGDynMeshSimplifyParameters Parameters = ToParameters();
	if (!Handle.IsEmptySelectionNoOp())
	{
		// Region restoration uses the extracted region's original boundary IDs. Compact only after it is welded
		// back into the base mesh or those IDs cease to describe the seam.
		PCGUtilsDynMeshSimplify::ApplySimplify(Handle.GetTargetMesh(), Parameters, bSelectionRegion);
	}
	if (!FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle))
	{
		return false;
	}

	if (bSelectionRegion && Parameters.WantsAutoCompact())
	{
		Handle.GetTargetMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh) { Mesh.CompactInPlace(); });
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
