// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshSimplifyParameters.h"

#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSimplifyParameters"

bool FPCGDynMeshSimplifyParameters::WantsAutoCompact() const
{
	switch (Mode)
	{
	case EPCGDynMeshSimplifyMode::Planar: return PlanarOptions.bAutoCompact;
	case EPCGDynMeshSimplifyMode::PolygroupTopology: return PolygroupOptions.bAutoCompact;
	case EPCGDynMeshSimplifyMode::TriangleCount:
	case EPCGDynMeshSimplifyMode::VertexCount:
	case EPCGDynMeshSimplifyMode::Tolerance:
	case EPCGDynMeshSimplifyMode::EdgeLength: return SimplifyOptions.bAutoCompact;
	default: return false;
	}
}

void PCGUtilsDynMeshSimplify::ApplySimplify(UDynamicMesh* Mesh, const FPCGDynMeshSimplifyParameters& Parameters,
	bool bSuppressAutoCompact)
{
	if (!Mesh)
	{
		return;
	}

	FGeometryScriptPlanarSimplifyOptions Planar = Parameters.PlanarOptions;
	FGeometryScriptPolygroupSimplifyOptions Polygroups = Parameters.PolygroupOptions;
	FGeometryScriptSimplifyMeshOptions General = Parameters.SimplifyOptions;
	if (bSuppressAutoCompact)
	{
		Planar.bAutoCompact = false;
		Polygroups.bAutoCompact = false;
		General.bAutoCompact = false;
	}

	switch (Parameters.Mode)
	{
	case EPCGDynMeshSimplifyMode::Planar:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPlanar(Mesh, Planar); break;
	case EPCGDynMeshSimplifyMode::PolygroupTopology:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPolygroupTopology(
			Mesh, Polygroups, Parameters.GroupLayer); break;
	case EPCGDynMeshSimplifyMode::TriangleCount:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(
			Mesh, Parameters.TargetTriangleCount, General); break;
	case EPCGDynMeshSimplifyMode::VertexCount:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToVertexCount(
			Mesh, Parameters.TargetVertexCount, General); break;
	case EPCGDynMeshSimplifyMode::Tolerance:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTolerance(
			Mesh, Parameters.GeometricTolerance, General); break;
	case EPCGDynMeshSimplifyMode::EdgeLength:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToEdgeLength(
			Mesh, Parameters.TargetEdgeLength, General); break;
	case EPCGDynMeshSimplifyMode::ClusterEdgeLength:
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyClusterSimplifyToEdgeLength(
			Mesh, Parameters.TargetEdgeLength, Parameters.ClusterOptions); break;
	}
}

FText PCGUtilsDynMeshSimplify::ModeText(EPCGDynMeshSimplifyMode Mode)
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

#undef LOCTEXT_NAMESPACE
