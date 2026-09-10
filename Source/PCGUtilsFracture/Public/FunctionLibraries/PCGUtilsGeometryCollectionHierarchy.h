// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class FGeometryCollection;

/**
 * The module's vocabulary for Geometry Collection hierarchy, in one place.
 *
 * A Geometry Collection's Transform group is a tree of BONES. Not every bone carries geometry:
 *
 *   - A PIECE is a bone that is simulated rigid AND owns a geometry range. Pieces are what render, convert to
 *     DynMesh, get pruned and get fractured. Fracture Mode calls these "leaves".
 *   - A CLUSTER is a bone with SimulationType == FST_Clustered. Its shape is the union of the pieces beneath
 *     it. Unreal's fracture cutters keep the cluster's *pre-fracture* geometry on it, hidden, rather than
 *     deleting it (PlanarCut: `bRemoveOldGeometry = false`), so a cluster may point at geometry that nothing
 *     should treat as a piece. The revision publisher removes that geometry by default.
 *   - A ROOT has no parent. `DynMesh To GC` always adds a geometry-less cluster root.
 *   - An EMBEDDED bone (FST_None) references an exemplar mesh; the module never creates one.
 *
 * `Level` is NOT part of the collection schema. It is an optional attribute the publisher materialises, so
 * every reader here falls back to walking Parent when it is absent rather than returning garbage.
 *
 * Everything here is read-only and O(1) or O(subtree); anything that needs the whole hierarchy at once
 * belongs in a selector built on FCollectionTransformSelectionFacade.
 */
namespace PCGUtilsGeometryCollectionHierarchy
{
	PCGUTILSFRACTURE_API bool IsValidBone(const FGeometryCollection& InCollection, int32 InBone);

	/** The bone owns a geometry range, whatever its simulation type. Cluster legacy geometry counts. */
	PCGUTILSFRACTURE_API bool HasGeometry(const FGeometryCollection& InCollection, int32 InBone);

	/** Rigid AND owns geometry: a fracture piece. This is the only test a mesh-shaped consumer should use. */
	PCGUTILSFRACTURE_API bool IsPiece(const FGeometryCollection& InCollection, int32 InBone);

	PCGUTILSFRACTURE_API bool IsCluster(const FGeometryCollection& InCollection, int32 InBone);
	PCGUTILSFRACTURE_API bool IsEmbedded(const FGeometryCollection& InCollection, int32 InBone);
	PCGUTILSFRACTURE_API bool IsRoot(const FGeometryCollection& InCollection, int32 InBone);

	/** At least one face of the bone's own geometry is visible. False for bones without geometry. */
	PCGUTILSFRACTURE_API bool HasVisibleGeometry(const FGeometryCollection& InCollection, int32 InBone);

	PCGUTILSFRACTURE_API int32 GetParent(const FGeometryCollection& InCollection, int32 InBone);

	PCGUTILSFRACTURE_API bool HasLevelAttribute(const FGeometryCollection& InCollection);

	/**
	 * Depth below the root (root == 0). Reads the Level attribute when present, otherwise walks Parent, so
	 * the answer is always valid for a valid bone. INDEX_NONE for an invalid bone.
	 */
	PCGUTILSFRACTURE_API int32 GetLevel(const FGeometryCollection& InCollection, int32 InBone);

	/** Every piece, ascending bone index. */
	PCGUTILSFRACTURE_API void GatherPieces(const FGeometryCollection& InCollection, TArray<int32>& OutBones);
	PCGUTILSFRACTURE_API void GatherClusters(const FGeometryCollection& InCollection, TArray<int32>& OutBones);
	PCGUTILSFRACTURE_API void GatherRoots(const FGeometryCollection& InCollection, TArray<int32>& OutBones);

	PCGUTILSFRACTURE_API int32 CountPieces(const FGeometryCollection& InCollection);
	PCGUTILSFRACTURE_API int32 CountClusters(const FGeometryCollection& InCollection);

	/**
	 * The pieces that make up a bone's shape: the bone itself if it is a piece, otherwise every piece found by
	 * descending through clusters. A rigid bone is treated as a leaf even if it has children, matching
	 * FCollectionTransformSelectionFacade::ConvertSelectionToRigidNodes and the fracture entry points.
	 */
	PCGUTILSFRACTURE_API void GatherPiecesUnder(
		const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones);

	/** Parent, grandparent, ... up to the root. Nearest first. */
	PCGUTILSFRACTURE_API void GetAncestors(
		const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones);

	/** Every bone strictly below InBone, depth first. */
	PCGUTILSFRACTURE_API void GetDescendants(
		const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones);

	/** INDEX_NONE when the bones share no ancestor (separate roots) or the input is empty. */
	PCGUTILSFRACTURE_API int32 LowestCommonAncestor(
		const FGeometryCollection& InCollection, TConstArrayView<int32> InBones);
}
