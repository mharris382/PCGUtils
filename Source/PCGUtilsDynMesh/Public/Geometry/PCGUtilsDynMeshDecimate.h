// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Blender-style Planar and Un-Subdivide decimation on an FDynamicMesh3.
 *
 * These are written from the documented behaviour of Blender's Decimate modifier, not translated from its
 * GPL source, so they can ship under PCGUtils' MIT license.
 *
 * Both modes work the same way. They first decide which triangles become one merged face (a "region"), and which
 * vertices on region outlines may go. Then every vertex inside a region, and every removable outline vertex, is
 * collapsed into a neighbor that keeps the outline in place. Finally, edges inside each region are flipped toward
 * a Delaunay triangulation. Collapses and flips go through FDynamicMesh3, so UVs, normals, colors, material IDs,
 * polygroups and other attributes stay consistent.
 */
namespace PCGUtilsDynMeshDecimate
{
	struct FPlanarOptions
	{
		/** Faces whose normals differ by less than this merge into one region. Degrees. */
		double AngleLimitDegrees = 5.0;
		/** Remove every outline vertex that sits between exactly two regions, whatever its angle. */
		bool bAllBoundaries = false;
		/** Never merge across these. */
		bool bDelimitMaterial = true;
		bool bDelimitPolygroup = false;
		bool bDelimitUVSeams = true;
		bool bDelimitNormalSeams = false;
	};

	struct FResult
	{
		int32 RemovedVertices = 0;
		/** Vertices chosen for removal that no valid collapse could remove. */
		int32 BlockedVertices = 0;
		int32 Regions = 0;
		/** Un-Subdivide: face-center vertices found, per iteration summed. */
		int32 GridCenters = 0;
	};

	/** Planar decimation. SelectedTriangles limits it to those triangles; null means the whole mesh. */
	PCGUTILSDYNMESH_API FResult DissolvePlanar(UE::Geometry::FDynamicMesh3& Mesh, const FPlanarOptions& Options,
		const TSet<int32>* SelectedTriangles = nullptr);

	/** Un-Subdivide decimation, repeated Iterations times. */
	PCGUTILSDYNMESH_API FResult Unsubdivide(UE::Geometry::FDynamicMesh3& Mesh, int32 Iterations,
		const TSet<int32>* SelectedTriangles = nullptr);
}
