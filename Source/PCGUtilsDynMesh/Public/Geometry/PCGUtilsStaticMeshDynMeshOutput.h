// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Conversion/PCGGetStaticMeshData.h"
#include "Geometry/PCGUtilsStaticMeshBrushCache.h"

struct FPCGContext;

/**
 * The DynMesh output of Get Static Mesh Data. The node lives in PCGUtils, which this module depends on, so the
 * module binds Emit to the node's handler when it loads.
 *
 * Every Static Mesh is read once per scale through FStaticMeshBrushCache, and a self-unioned group is stored there
 * as a combined entry keyed by its meshes, their scales, their placement relative to each other and the union
 * options. A run where none of that changed copies the stored mesh instead of unioning again. The cache drops an
 * entry when a Static Mesh it was built from reports a change (reimport, edit, rebuild), so only the groups using
 * that asset are rebuilt.
 */
namespace PCGUtilsStaticMeshDynMeshOutput
{
	/**
	 * Splits the placements into the groups that each become one DynMesh. A group lists indices into Placements, in
	 * input order; groups are ordered by their first member. By Overlapping joins placements whose oriented bounding
	 * boxes, grown by OverlapPadding on every side, intersect, directly or through a chain of others.
	 */
	PCGUTILSDYNMESH_API void GroupPlacements(TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements,
		EPCGUtilsStaticMeshMergeBehavior MergeBehavior, double OverlapPadding, TArray<TArray<int32>>& OutGroups);

	struct FGroupMesh
	{
		/** The group's mesh in Frame space. Shared with the cache: copy it before changing it. Null when no mesh could be read. */
		TSharedPtr<const FStaticMeshBrushCachedMesh> Mesh;
		/** Frame space to world: the rotation and location of one of the group's placements, without scale. */
		FTransform Frame = FTransform::Identity;
		/** True when a self-unioned result was found in the cache instead of being built. */
		bool bFromCache = false;
	};

	/**
	 * The mesh of one group: its placements appended, then self-unioned when asked. Only self-unioned results are
	 * stored as combined cache entries; without a union the append is cheap and is redone from the per-asset entries.
	 */
	PCGUTILSDYNMESH_API FGroupMesh FindOrBuildGroupMesh(TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements,
		TConstArrayView<int32> Group, bool bSelfUnion, const FGeometryScriptMeshSelfUnionOptions& SelfUnionOptions,
		TArray<FText>& OutWarnings);

	/** Groups the placements, builds or finds each group's mesh and adds one DynMesh data per group to the context's output. */
	PCGUTILSDYNMESH_API void Emit(FPCGContext* Context, const UPCGGetStaticMeshDataSettings* Settings,
		TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements);
}
