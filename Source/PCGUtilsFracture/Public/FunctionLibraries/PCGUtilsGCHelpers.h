// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class FGeometryCollection;

/**
 * Small shared helpers for reading Geometry Collection hierarchy/geometry state. Deliberately thin: anything
 * Epic already implements (selection algorithms, prune, fracture) is called directly rather than wrapped.
 */
namespace PCGUtilsGCHelpers
{
	/**
	 * A bone that actually carries geometry and is simulated as a rigid leaf, i.e. a fracture piece rather
	 * than a structural cluster. This is what "geometry-bearing leaf bone" means throughout this module.
	 */
	PCGUTILSFRACTURE_API bool IsGeometryBearingBone(const FGeometryCollection& InCollection, int32 InBoneIndex);

	/** Every geometry-bearing bone, ascending. */
	PCGUTILSFRACTURE_API void GatherGeometryBearingBones(
		const FGeometryCollection& InCollection, TArray<int32>& OutBoneIndices);

	/**
	 * Bone transforms resolved from parent-relative to collection space. Stored Transform[] is relative to the
	 * parent, so anything spatial must go through here - the same thing FFractureEngineFracturing does
	 * internally before computing its own bounds.
	 */
	PCGUTILSFRACTURE_API void ComputeGlobalTransforms(
		const FGeometryCollection& InCollection, TArray<FTransform>& OutGlobalTransforms);

	/** The bone's geometry bounds, in that bone's own local space. Invalid box when the bone has no geometry. */
	PCGUTILSFRACTURE_API FBox GetBoneLocalBounds(const FGeometryCollection& InCollection, int32 InBoneIndex);

	/**
	 * Rewrites MaterialID on every face tagged Internal.
	 *
	 * FFractureEngineFracturing::VoronoiFracture does not expose an internal material id - its
	 * FInternalSurfaceMaterials::GlobalMaterialID defaults to 0 - so this is the post-pass that gives one.
	 * Note it retags *all* internal faces, including any produced by an earlier fracture in the same chain.
	 *
	 * @return number of faces changed.
	 */
	PCGUTILSFRACTURE_API int32 SetInternalFaceMaterialID(FGeometryCollection& InOutCollection, int32 InMaterialID);

	/** "bones: 57 (46 geometry), faces: 12480, vertices: 6203" - for one-line summary logging. */
	PCGUTILSFRACTURE_API FString DescribeCollection(const FGeometryCollection& InCollection);

	/**
	 * Checks the attributes every FFractureEngineFracturing entry point requires before it will do anything.
	 *
	 * Those entry points guard on these silently and just return INDEX_NONE, which is indistinguishable from
	 * "the cut produced nothing" - so we check up front and name the missing attribute instead. Producers
	 * validate their output with this; consumers validate their input.
	 *
	 * @return true if the collection is ready to be fractured.
	 */
	PCGUTILSFRACTURE_API bool ValidateFractureRequirements(
		const FGeometryCollection& InCollection, TArray<FString>& OutMissingAttributes);

	/**
	 * Bounds of all geometry-bearing bones in collection space, computed the same way the fracture backend
	 * computes its own: per-bone bounds transformed by the bone's global matrix.
	 */
	PCGUTILSFRACTURE_API FBox ComputeCollectionBounds(const FGeometryCollection& InCollection);
}
