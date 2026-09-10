// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

class FGeometryCollection;
class UMaterialInterface;
class UPCGGeometryCollectionData;
struct FPCGContext;

/**
 * What a mutating operation did to a collection.
 *
 * Reported by every fracture/edit operation and consumed by the publisher, which uses it to decide what has to
 * be renormalised. It is also the hook a derived-data cache will use to decide what it can keep, so an
 * operation that under-reports is a correctness bug rather than a performance one: when in doubt, report more.
 */
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionMutationResult
{
	/** Bone Transform values changed. Positions moved; no element was added, removed or reordered. */
	bool bTransformsChanged = false;

	/** Vertices, faces or their attributes changed on geometry that already existed. */
	bool bGeometryChanged = false;

	/** Parent, Children or SimulationType changed. Level must be regenerated. */
	bool bHierarchyChanged = false;

	/**
	 * Elements were removed or reordered in some group, so an index taken before the operation may now name a
	 * different element. Pure appends do NOT set this - see FirstNewTransformIndex.
	 */
	bool bStructureChanged = false;

	/**
	 * Set when transforms were appended and nothing was removed or reordered, i.e. every bone below this index
	 * still means what it meant before. This is the case for Unreal's fracture cutters, which append the new
	 * pieces and leave the existing bones alone. INDEX_NONE when the operation was not a pure append.
	 */
	int32 FirstNewTransformIndex = INDEX_NONE;

	/**
	 * Geometry indices whose contents changed, valid against the PRE-mutation indexing. Empty means "assume
	 * every geometry in the flagged categories changed"; it is only a narrowing hint, never a widening one.
	 */
	TSet<int32> DirtyGeometryIndices;

	bool AnythingChanged() const
	{
		return bTransformsChanged || bGeometryChanged || bHierarchyChanged || bStructureChanged
			|| FirstNewTransformIndex != INDEX_NONE;
	}

	/** Everything changed; the safe answer when an operation cannot describe itself. */
	static FPCGUtilsGeometryCollectionMutationResult Everything();

	/** A cutter that appended new bones from InFirstNewTransformIndex and reworked the bones it cut. */
	static FPCGUtilsGeometryCollectionMutationResult Fracture(int32 InFirstNewTransformIndex);

	/** Bones were deleted, so every index may have shifted. */
	static FPCGUtilsGeometryCollectionMutationResult Structural();

	/**
	 * Folds another operation's result into this one, for an executor running several operations in sequence.
	 * Fine-grained hints are dropped whenever they can no longer be compared: two appends keep the earlier
	 * FirstNewTransformIndex, but anything structural discards both that and the dirty-geometry set.
	 */
	void Accumulate(const FPCGUtilsGeometryCollectionMutationResult& InOther);
};

/** Normalisation the publisher performs on its way out. Defaults are what every current element wants. */
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionPublishOptions
{
	/**
	 * Delete the geometry of cluster bones whose faces are all invisible.
	 *
	 * Unreal's cutters do not remove the geometry they replace: CutMultipleWithPlanarCells keeps it and marks
	 * every face invisible (`bRemoveOldGeometry = false`), leaving the cut bone a cluster that still owns a
	 * full copy of its pre-fracture shape. Nothing downstream can use it - the engine converter, Fracture Mode
	 * and this module all skip non-rigid bones - but it is carried through every later operation, and a second
	 * fracture level stacks another copy on top. Removing it only touches the Geometry group, so bone indices
	 * are unaffected.
	 */
	bool bCompactHiddenGeometry = true;

	/**
	 * Materialise the Level attribute when it is missing or the hierarchy changed.
	 *
	 * Level is not part of the collection schema - FTransformCollection::Construct does not create it - yet
	 * every engine selector that mentions a level (SelectLevel, GetBonesByLevel, EnumerateNeighbors, and so
	 * the whole Contact family) silently returns nothing or ensures without it. Publishing it once here is
	 * what makes those usable at all.
	 */
	bool bRegenerateLevel = true;
};

/**
 * The single route by which a mutated Geometry Collection becomes PCG data.
 *
 * Every mutating element used to end with the same hand-rolled sequence - copy, mutate, ReindexMaterials,
 * InitializeAsRevisionOf - and each one normalised a slightly different subset, which is how collections
 * without a Level attribute reached selectors that need one. Centralising it makes "what a published
 * collection is guaranteed to have" a property of the module rather than of each node, and gives the future
 * geometry-view cache one place to decide what survives a mutation.
 *
 * A published collection is guaranteed to have: a Level attribute consistent with its hierarchy, a BoneId per
 * bone, material sections consistent with its face MaterialIDs, geometry bounds consistent with its vertices,
 * no stale Proximity, and (by default) no hidden cluster geometry.
 */
namespace PCGUtilsGeometryCollectionRevisionPublisher
{
	/**
	 * Publishes a mutated copy as the next revision of InSource's lineage: same CollectionId, Revision + 1,
	 * fresh StateId, materials inherited.
	 *
	 * @param InCollection  A private copy from CreateMutableCopy(), mutated in place. Ownership passes here.
	 * @param InMutation    What the operation changed. Drives which normalisation steps run.
	 * @return the new data, or nullptr if it could not be created.
	 */
	PCGUTILSFRACTURE_API UPCGGeometryCollectionData* PublishRevision(
		FPCGContext* InContext,
		const UPCGGeometryCollectionData* InSource,
		const TSharedRef<FGeometryCollection>& InCollection,
		const FPCGUtilsGeometryCollectionMutationResult& InMutation,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions = FPCGUtilsGeometryCollectionPublishOptions());

	/** Starts a fresh lineage from an authored collection. Everything is normalised unconditionally. */
	PCGUTILSFRACTURE_API UPCGGeometryCollectionData* PublishNewLineage(
		FPCGContext* InContext,
		const TSharedRef<FGeometryCollection>& InCollection,
		TArray<TObjectPtr<UMaterialInterface>> InMaterials,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions = FPCGUtilsGeometryCollectionPublishOptions());

	/**
	 * Runs the normalisation steps in place without creating PCG data. Exposed for tests and for any future
	 * caller that owns a collection outside the data lifetime; elements should call a Publish function.
	 *
	 * @return number of geometry elements removed by hidden-geometry compaction.
	 */
	PCGUTILSFRACTURE_API int32 Normalize(
		FGeometryCollection& InOutCollection,
		const FPCGUtilsGeometryCollectionMutationResult& InMutation,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions);

	/**
	 * Geometry indices belonging to cluster bones with no visible faces - the hidden pre-fracture shapes
	 * described on bCompactHiddenGeometry. Sorted ascending, ready for RemoveElements.
	 */
	PCGUTILSFRACTURE_API void GatherHiddenClusterGeometry(
		const FGeometryCollection& InCollection, TArray<int32>& OutGeometryIndices);
}
