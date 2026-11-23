// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshPointConversion.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshPointConversion"

namespace PCGUtilsDynMeshPointConversionPrivate
{
	/** A point at WorldCenter whose bounds are MeshBox (mesh space), kept exactly where the mesh transform puts it. */
	void AddBoxPoint(FPCGUtilsElementPoints& Out, const FTransform& MeshToWorld, const FVector& MeshCenter, const FBox& MeshBox)
	{
		FTransform PointTransform = MeshToWorld;
		PointTransform.SetLocation(MeshToWorld.TransformPosition(MeshCenter));
		Out.AddPoint(PointTransform, MeshBox.ShiftBy(-MeshCenter));
	}
}

bool PCGUtilsDynMeshElementPoints::ElementsToPoints(
	const FPCGUtilsDynMeshPointConversion& Conversion,
	const FPCGUtilsDynMeshSelectionEvaluationContext& EvaluationContext,
	FPCGContext* Context,
	FPCGUtilsElementPoints& OutPoints,
	FText& OutError)
{
	using namespace UE::Geometry;
	using namespace PCGUtilsDynMeshPointConversionPrivate;

	if (EvaluationContext.Domain.TopologyType != EGeometryTopologyType::Triangle)
	{
		OutError = LOCTEXT("PolygroupTopology",
			"the selection's topology is Polygroup; mesh elements are turned into points on Triangle topology only (vertices, edges or triangles).");
		return false;
	}
	const EGeometryElementType ElementType = EvaluationContext.Domain.ElementType;
	if (ElementType != EGeometryElementType::Vertex && ElementType != EGeometryElementType::Edge && ElementType != EGeometryElementType::Face)
	{
		OutError = FText::Format(LOCTEXT("ElementType",
			"the selection's element type is {0}; expected Vertex, Edge or Face."), FText::AsNumber(static_cast<int32>(ElementType)));
		return false;
	}
	if (Conversion.Match == EPCGUtilsElementPointMatch::AtLeastFraction && (Conversion.MatchFraction < 0.0 || Conversion.MatchFraction > 1.0))
	{
		OutError = FText::Format(LOCTEXT("MatchFraction",
			"DynMesh Conversion: Match Fraction is {0}; expected a value from 0 to 1."), FText::AsNumber(Conversion.MatchFraction));
		return false;
	}

	OutPoints.Match = Conversion.Match;
	OutPoints.MatchFraction = Conversion.MatchFraction;

	// The mesh is target-actor-local and points are in the world: world = actor * mesh. Without a PCG context
	// there is no target actor to place the mesh by; the mesh is then read as already being in the world.
	const FTransform MeshToWorld = Context
		? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, EvaluationContext.MeshData, /*bConvertToLocalSpace=*/true)
		: FTransform::Identity;
	const FDynamicMesh3& Mesh = EvaluationContext.Mesh;
	auto World = [&MeshToWorld, &Mesh](int32 VertexID)
	{
		return MeshToWorld.TransformPosition(FVector(Mesh.GetVertex(VertexID)));
	};

	if (ElementType == EGeometryElementType::Vertex)
	{
		OutPoints.Reserve(Mesh.VertexCount(), Mesh.VertexCount());
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			OutPoints.BeginElement(VertexID);
			OutPoints.AddPoint(World(VertexID));
		}
		return true;
	}

	if (ElementType == EGeometryElementType::Edge)
	{
		const bool bVertices = Conversion.EdgePoints == EPCGUtilsDynMeshEdgePoints::Vertices;
		OutPoints.Reserve(Mesh.EdgeCount(), Mesh.EdgeCount() * (bVertices ? 2 : 1));
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			const FIndex2i Edge = Mesh.GetEdgeV(EdgeID);
			OutPoints.BeginElement(EdgeID);
			if (bVertices)
			{
				OutPoints.AddPoint(World(Edge.A));
				OutPoints.AddPoint(World(Edge.B));
				continue;
			}
			const FVector A(Mesh.GetVertex(Edge.A));
			const FVector B(Mesh.GetVertex(Edge.B));
			const FVector Mid = (A + B) * 0.5;
			if (Conversion.EdgePoints == EPCGUtilsDynMeshEdgePoints::MidpointWithBounds)
			{
				FBox Box(ForceInit);
				Box += A;
				Box += B;
				AddBoxPoint(OutPoints, MeshToWorld, Mid, Box);
			}
			else
			{
				OutPoints.AddPoint(MeshToWorld.TransformPosition(Mid));
			}
		}
		return true;
	}

	const bool bVertices = Conversion.TrianglePoints == EPCGUtilsDynMeshTrianglePoints::Vertices;
	OutPoints.Reserve(Mesh.TriangleCount(), Mesh.TriangleCount() * (bVertices ? 3 : 1));
	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		OutPoints.BeginElement(TriangleID);
		if (bVertices)
		{
			OutPoints.AddPoint(World(Triangle.A));
			OutPoints.AddPoint(World(Triangle.B));
			OutPoints.AddPoint(World(Triangle.C));
			continue;
		}
		const FVector A(Mesh.GetVertex(Triangle.A));
		const FVector B(Mesh.GetVertex(Triangle.B));
		const FVector C(Mesh.GetVertex(Triangle.C));
		const FVector Center = (A + B + C) / 3.0;
		if (Conversion.TrianglePoints == EPCGUtilsDynMeshTrianglePoints::CenterWithBounds)
		{
			FBox Box(ForceInit);
			Box += A;
			Box += B;
			Box += C;
			AddBoxPoint(OutPoints, MeshToWorld, Center, Box);
		}
		else
		{
			OutPoints.AddPoint(MeshToWorld.TransformPosition(Center));
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
