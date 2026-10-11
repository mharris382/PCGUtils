// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

/** Element normals as selection predicates read them. */
namespace PCGDynMeshSelectionNormalHelpers
{
	/**
	 * A vertex's normal: the first one the normal overlay holds for it, else its per-vertex normal, else
	 * Fallback. Not normalized.
	 */
	inline FVector3f GetVertexNormal(
		const UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FDynamicMeshNormalOverlay* Overlay,
		int32 VertexID, const FVector3f& Fallback)
	{
		if (Overlay)
		{
			FVector3f Result = Fallback;
			bool bFound = false;
			Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
			{
				if (bFound || !Overlay->IsSetTriangle(TriangleID))
				{
					return;
				}
				const UE::Geometry::FIndex3i TriangleVertices = Mesh.GetTriangle(TriangleID);
				const UE::Geometry::FIndex3i TriangleElements = Overlay->GetTriangle(TriangleID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (TriangleVertices[Corner] == VertexID && Overlay->IsElement(TriangleElements[Corner]))
					{
						Result = Overlay->GetElement(TriangleElements[Corner]);
						bFound = true;
						break;
					}
				}
			});
			return Result;
		}
		return Mesh.HasVertexNormals() ? Mesh.GetVertexNormal(VertexID) : Fallback;
	}

	/** The area-weighted average of the face normals around a vertex. Zero for a vertex with no triangles. */
	inline FVector3d GetAverageFaceNormal(const UE::Geometry::FDynamicMesh3& Mesh, int32 VertexID)
	{
		FVector3d Sum = FVector3d::Zero();
		Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
		{
			FVector3d Normal, Centroid;
			double Area = 0.0;
			Mesh.GetTriInfo(TriangleID, Normal, Area, Centroid);
			Sum += Normal * Area;
		});
		return Sum;
	}
}
