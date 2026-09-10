// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionSurface.h"

#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace PCGUtilsGeometryCollectionSurface
{
	namespace
	{
		using namespace UE::Geometry;

		/** The named layer, or null when this mesh does not carry one. */
		const FDynamicMeshPolygroupAttribute* FindLayer(const FDynamicMesh3& InMesh, FName InLayerName)
		{
			if (!InMesh.HasAttributes())
			{
				return nullptr;
			}

			const FDynamicMeshAttributeSet* Attributes = InMesh.Attributes();
			for (int32 Index = 0; Index < Attributes->NumPolygroupLayers(); ++Index)
			{
				const FDynamicMeshPolygroupAttribute* Layer = Attributes->GetPolygroupLayer(Index);
				if (Layer && Layer->GetName() == InLayerName)
				{
					return Layer;
				}
			}
			return nullptr;
		}

		const FDynamicMeshPolygroupAttribute* FindInternalLayer(const FDynamicMesh3& InMesh)
		{
			return FindLayer(InMesh, PCGUtilsGeometryCollectionPieceMesh::InternalFacePolygroupLayerName());
		}

		/**
		 * True when every incident triangle is interior, which is what makes the classification
		 * exterior-biased: one original-surface triangle is enough to call the element exterior.
		 *
		 * An element with no incident triangles is not interior - there is no cut surface it belongs to.
		 */
		template<typename EnumerateTrianglesType>
		bool AllIncidentTrianglesInterior(
			const FDynamicMesh3& InMesh,
			const FDynamicMeshPolygroupAttribute* InInternalLayer,
			EnumerateTrianglesType&& EnumerateTriangles)
		{
			if (!InInternalLayer)
			{
				return false;
			}

			bool bAnyTriangle = false;
			bool bAllInterior = true;
			EnumerateTriangles([&](int32 TriangleID)
			{
				bAnyTriangle = true;
				bAllInterior &= (InInternalLayer->GetValue(TriangleID) == 2);
			});
			return bAnyTriangle && bAllInterior;
		}
	}

	bool HasClassification(const UE::Geometry::FDynamicMesh3& InMesh)
	{
		return FindInternalLayer(InMesh) != nullptr;
	}

	bool IsInteriorTriangle(const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID)
	{
		const FDynamicMeshPolygroupAttribute* Layer = FindInternalLayer(InMesh);
		// The engine's encoding: 1 is an original surface, 2 is a fracture cut.
		return Layer && InMesh.IsTriangle(InTriangleID) && Layer->GetValue(InTriangleID) == 2;
	}

	bool IsInteriorVertex(const UE::Geometry::FDynamicMesh3& InMesh, int32 InVertexID)
	{
		if (!InMesh.IsVertex(InVertexID))
		{
			return false;
		}

		const FDynamicMeshPolygroupAttribute* Layer = FindInternalLayer(InMesh);
		return AllIncidentTrianglesInterior(InMesh, Layer, [&](auto&& Visit)
		{
			for (const int32 TriangleID : InMesh.VtxTrianglesItr(InVertexID))
			{
				Visit(TriangleID);
			}
		});
	}

	bool IsInteriorEdge(const UE::Geometry::FDynamicMesh3& InMesh, int32 InEdgeID)
	{
		if (!InMesh.IsEdge(InEdgeID))
		{
			return false;
		}

		const FDynamicMeshPolygroupAttribute* Layer = FindInternalLayer(InMesh);
		const FIndex2i EdgeTriangles = InMesh.GetEdgeT(InEdgeID);
		return AllIncidentTrianglesInterior(InMesh, Layer, [&](auto&& Visit)
		{
			if (EdgeTriangles.A != FDynamicMesh3::InvalidID) { Visit(EdgeTriangles.A); }
			if (EdgeTriangles.B != FDynamicMesh3::InvalidID) { Visit(EdgeTriangles.B); }
		});
	}

	bool IsVisibleTriangle(const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID)
	{
		const FDynamicMeshPolygroupAttribute* Layer =
			FindLayer(InMesh, PCGUtilsGeometryCollectionPieceMesh::VisibleFacePolygroupLayerName());
		if (!Layer || !InMesh.IsTriangle(InTriangleID))
		{
			// No layer means nothing ever marked this mesh's faces hidden, so they are all visible.
			return InMesh.IsTriangle(InTriangleID);
		}
		return Layer->GetValue(InTriangleID) == 2;
	}

	bool TriangleMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget)
	{
		switch (InTarget)
		{
		case EPCGUtilsGeometryCollectionSurfaceTarget::Interior:
			return IsInteriorTriangle(InMesh, InTriangleID);
		case EPCGUtilsGeometryCollectionSurfaceTarget::Exterior:
			return InMesh.IsTriangle(InTriangleID) && !IsInteriorTriangle(InMesh, InTriangleID);
		default:
			return InMesh.IsTriangle(InTriangleID);
		}
	}

	bool VertexMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InVertexID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget)
	{
		switch (InTarget)
		{
		case EPCGUtilsGeometryCollectionSurfaceTarget::Interior:
			return IsInteriorVertex(InMesh, InVertexID);
		case EPCGUtilsGeometryCollectionSurfaceTarget::Exterior:
			return InMesh.IsVertex(InVertexID) && !IsInteriorVertex(InMesh, InVertexID);
		default:
			return InMesh.IsVertex(InVertexID);
		}
	}

	bool EdgeMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InEdgeID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget)
	{
		switch (InTarget)
		{
		case EPCGUtilsGeometryCollectionSurfaceTarget::Interior:
			return IsInteriorEdge(InMesh, InEdgeID);
		case EPCGUtilsGeometryCollectionSurfaceTarget::Exterior:
			return InMesh.IsEdge(InEdgeID) && !IsInteriorEdge(InMesh, InEdgeID);
		default:
			return InMesh.IsEdge(InEdgeID);
		}
	}

	FSurfaceCounts MeasureSurface(const UE::Geometry::FDynamicMesh3& InMesh)
	{
		FSurfaceCounts Counts;
		const FDynamicMeshPolygroupAttribute* Layer = FindInternalLayer(InMesh);

		for (const int32 TriangleID : InMesh.TriangleIndicesItr())
		{
			const bool bInterior = Layer && Layer->GetValue(TriangleID) == 2;
			(bInterior ? Counts.InteriorTriangles : Counts.ExteriorTriangles) += 1;

			FVector3d A, B, C;
			InMesh.GetTriVertices(TriangleID, A, B, C);
			const double Area = 0.5 * FVector3d::CrossProduct(B - A, C - A).Size();

			(bInterior ? Counts.InteriorArea : Counts.ExteriorArea) += Area;
		}

		return Counts;
	}
}
