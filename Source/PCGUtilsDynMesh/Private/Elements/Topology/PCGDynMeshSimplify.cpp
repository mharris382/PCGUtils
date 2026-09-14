// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshSimplify.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSimplify"

namespace
{
	FText DynMeshSimplifyModeText(EPCGDynMeshSimplifyMode Mode)
	{
		switch (Mode)
		{
		case EPCGDynMeshSimplifyMode::Planar: return LOCTEXT("Planar", "Planar");
		case EPCGDynMeshSimplifyMode::PolygroupTopology: return LOCTEXT("Polygroups", "Polygroup Topology");
		case EPCGDynMeshSimplifyMode::TriangleCount: return LOCTEXT("Triangles", "Triangle Count");
		case EPCGDynMeshSimplifyMode::VertexCount: return LOCTEXT("Vertices", "Vertex Count");
		case EPCGDynMeshSimplifyMode::Tolerance: return LOCTEXT("Tolerance", "Tolerance");
		case EPCGDynMeshSimplifyMode::EdgeLength: return LOCTEXT("EdgeLength", "Edge Length");
		case EPCGDynMeshSimplifyMode::ClusterEdgeLength: return LOCTEXT("Cluster", "Cluster Edge Length");
		default: return FText::GetEmpty();
		}
	}

	void ApplyDynMeshSimplify(UDynamicMesh* Mesh, const FPCGUtilsDynMeshSimplifyOperation& Operation,
		bool bSelectionRegion)
	{
		FGeometryScriptPlanarSimplifyOptions Planar = Operation.PlanarOptions;
		FGeometryScriptPolygroupSimplifyOptions Polygroups = Operation.PolygroupOptions;
		FGeometryScriptSimplifyMeshOptions General = Operation.SimplifyOptions;
		if (bSelectionRegion)
		{
			// Region restoration uses the extracted region's original boundary IDs. Compact only after it is welded
			// back into the base mesh or those IDs cease to describe the seam.
			Planar.bAutoCompact = false;
			Polygroups.bAutoCompact = false;
			General.bAutoCompact = false;
		}

		switch (Operation.Mode)
		{
		case EPCGDynMeshSimplifyMode::Planar:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPlanar(Mesh, Planar); break;
		case EPCGDynMeshSimplifyMode::PolygroupTopology:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPolygroupTopology(
				Mesh, Polygroups, Operation.GroupLayer); break;
		case EPCGDynMeshSimplifyMode::TriangleCount:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(
				Mesh, Operation.TargetTriangleCount, General); break;
		case EPCGDynMeshSimplifyMode::VertexCount:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToVertexCount(
				Mesh, Operation.TargetVertexCount, General); break;
		case EPCGDynMeshSimplifyMode::Tolerance:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTolerance(
				Mesh, Operation.GeometricTolerance, General); break;
		case EPCGDynMeshSimplifyMode::EdgeLength:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToEdgeLength(
				Mesh, Operation.TargetEdgeLength, General); break;
		case EPCGDynMeshSimplifyMode::ClusterEdgeLength:
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyClusterSimplifyToEdgeLength(
				Mesh, Operation.TargetEdgeLength, Operation.ClusterOptions); break;
		}
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSimplifySettings::GetDefaultNodeTitle() const
{
	return FText::Format(LOCTEXT("Title", "DynMesh | Simplify {0}"), DynMeshSimplifyModeText(Mode));
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
	if (!Handle.IsEmptySelectionNoOp())
	{
		ApplyDynMeshSimplify(Handle.GetTargetMesh(), *this, bSelectionRegion);
	}
	if (!FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle))
	{
		return false;
	}

	const bool bWantsCompact =
		(Mode == EPCGDynMeshSimplifyMode::Planar && PlanarOptions.bAutoCompact)
		|| (Mode == EPCGDynMeshSimplifyMode::PolygroupTopology && PolygroupOptions.bAutoCompact)
		|| ((Mode == EPCGDynMeshSimplifyMode::TriangleCount || Mode == EPCGDynMeshSimplifyMode::VertexCount
			|| Mode == EPCGDynMeshSimplifyMode::Tolerance || Mode == EPCGDynMeshSimplifyMode::EdgeLength)
			&& SimplifyOptions.bAutoCompact);
	if (bSelectionRegion && bWantsCompact)
	{
		Handle.GetTargetMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh) { Mesh.CompactInPlace(); });
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
