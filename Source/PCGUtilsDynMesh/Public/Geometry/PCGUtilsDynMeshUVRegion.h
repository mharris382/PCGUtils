// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "IndexTypes.h"
#include "Templates/Function.h"

struct FPCGContext;
class UDynamicMesh;

/**
 * Helpers shared by the UV nodes. They edit the UVs of a triangle region (a selection, or the whole mesh) without
 * touching UVs outside it:
 *
 * - Gather copies the region's UVs out of the overlay into a flat list, keeping which corners share a UV.
 * - The caller edits the list (moves, packs, rescales islands).
 * - WriteBack puts them in again. A region that is the whole mesh replaces the overlay outright; a partial region
 *   gets new UV elements, so a UV the region shared with an unselected triangle is split rather than dragged along.
 */
namespace PCGUtilsDynMeshUV
{
	struct PCGUTILSDYNMESH_API FRegionUVs
	{
		/** Base-mesh triangle IDs with UVs set, in region order. Triangles without UVs are left out. */
		TArray<int32> Triangles;
		/** Per entry of Triangles: indices into UVs for its three corners. */
		TArray<UE::Geometry::FIndex3i> Corners;
		/** The UV values. Corners that shared an overlay element share an index here. */
		TArray<FVector2f> UVs;
		/** True when the region is every triangle of the mesh. */
		bool bWholeMesh = false;
		/** Triangles of the region that had no UVs, so were not gathered. */
		int32 NumUnset = 0;
	};

	/** Copies the UVs of TriangleIDs out of Overlay. */
	PCGUTILSDYNMESH_API void Gather(const UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FDynamicMeshUVOverlay& Overlay,
		TArrayView<const int32> TriangleIDs, FRegionUVs& Out);

	/** Writes Region back into Overlay (see the namespace comment). */
	PCGUTILSDYNMESH_API void WriteBack(UE::Geometry::FDynamicMeshUVOverlay& Overlay, const FRegionUVs& Region);

	/** UV islands of a gathered region: lists of indices into Region.Triangles, joined where corners share a UV. */
	PCGUTILSDYNMESH_API TArray<TArray<int32>> FindIslands(const FRegionUVs& Region);

	/** The UV indices an island uses, each once. */
	PCGUTILSDYNMESH_API TArray<int32> IslandUVIndices(const FRegionUVs& Region, TConstArrayView<int32> IslandTriangles);

	/** 3D surface area of some region triangles, and their UV area (absolute). */
	PCGUTILSDYNMESH_API void IslandAreas(const UE::Geometry::FDynamicMesh3& Mesh, const FRegionUVs& Region,
		TConstArrayView<int32> IslandTriangles, double& OutSurfaceArea, double& OutUVArea);

	/**
	 * Runs a whole-mesh Geometry Script UV function on just TriangleIDs: on a copy of the mesh, or a submesh of the
	 * region when it is partial, then writes the resulting UVs of layer UVLayer back onto those triangles. Fn gets the
	 * working mesh and returns false on failure. The working UDynamicMesh is made through PCGUtilsAsyncObjects, so
	 * this is safe in a parallel run. Returns false when Fn fails or changed the working mesh's triangles.
	 */
	PCGUTILSDYNMESH_API bool RunOnRegion(FPCGContext* Context, UE::Geometry::FDynamicMesh3& Mesh, int32 UVLayer,
		TArrayView<const int32> TriangleIDs, TFunctionRef<bool(UDynamicMesh*)> Fn);
}
