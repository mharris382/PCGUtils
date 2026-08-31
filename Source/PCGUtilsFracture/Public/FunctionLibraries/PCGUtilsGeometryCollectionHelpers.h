// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class FGeometryCollection;

/**
 * Small shared helpers for reading Geometry Collection hierarchy/geometry state. Deliberately thin: anything
 * Epic already implements (selection algorithms, prune, fracture) is called directly rather than wrapped.
 */
namespace PCGUtilsGeometryCollectionHelpers
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
	 * Per-bone surface, split into original (exterior) and fracture-generated (interior) faces.
	 *
	 * The collection tracks an Internal flag per *face*, so this is a genuine breakdown rather than a boolean:
	 * a bone knows how much of its surface was inherited from the source mesh and how much a cut created.
	 */
	struct FBoneSurfaceInfo
	{
		int32 ExteriorFaceCount = 0;
		int32 InteriorFaceCount = 0;
		double ExteriorArea = 0.0;
		double InteriorArea = 0.0;

		/** A bone with no exterior faces is buried: removing it changes no silhouette. */
		bool IsExterior() const { return ExteriorFaceCount > 0; }

		int32 TotalFaceCount() const { return ExteriorFaceCount + InteriorFaceCount; }
		double TotalArea() const { return ExteriorArea + InteriorArea; }

		/**
		 * Fraction of this piece's surface that was originally on the outside, in [0,1].
		 *
		 * More useful than either raw area for choosing pieces, because it is scale-invariant: 0 is fully
		 * buried, a value near 1 is a piece the fracture barely touched, and the middle is a chunk with real
		 * exposure. An absolute area threshold has to be retuned for every mesh size; this does not.
		 */
		double ExposureRatio() const
		{
			const double Total = TotalArea();
			return Total > UE_DOUBLE_SMALL_NUMBER ? ExteriorArea / Total : 0.0;
		}
	};

	/**
	 * Measures one bone's exterior/interior surface split.
	 *
	 * Reads the collection's own per-face `Internal` flag rather than inferring anything geometrically:
	 * AppendMeshToCollection marks original faces external, PlanarCut marks the faces it creates internal, and
	 * that flag round-trips through every subsequent cut. Exact, and one pass over the bone's face range.
	 *
	 * @param InBoneToCollection The bone's global transform, so areas are measured in collection space rather
	 *                           than bone-local space. Only matters for a scaled bone.
	 */
	PCGUTILSFRACTURE_API FBoneSurfaceInfo GetBoneSurfaceInfo(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		const FTransform& InBoneToCollection = FTransform::Identity);

	/**
	 * One adjacency edge between two geometry-bearing bones.
	 *
	 * Contact fields are only filled when contact measurement was requested; they stay at zero otherwise,
	 * because computing them requires convex hulls and is markedly more expensive than adjacency alone.
	 */
	struct FBoneAdjacencyEdge
	{
		int32 BoneA = INDEX_NONE;
		int32 BoneB = INDEX_NONE;

		/** Estimated area of the contact region between the two pieces. */
		float ContactArea = 0.0f;

		/** Width of the contact where it is thin - distinguishes a face weld from a corner touch. */
		float SharpContactWidth = 0.0f;
	};

	/**
	 * Builds the adjacency graph over geometry-bearing bones: which fracture pieces actually touch.
	 *
	 * Uses the engine's own proximity computation rather than inferring contact from bounds. Precise proximity
	 * looks for touching vertices or touching coplanar opposite-facing triangles, which is exactly the shape
	 * fracture cuts produce - Epic's own comment calls it out as the mode suited to their fracture tools.
	 *
	 * Each pair appears once, ordered so BoneA < BoneB.
	 *
	 * @param bComputeContact  Also measure contact area and sharp-contact width. Requires generating convex
	 *                         hulls for every piece, so it is considerably slower than adjacency alone.
	 * @return false if the collection has no usable proximity information.
	 */
	PCGUTILSFRACTURE_API bool BuildBoneAdjacency(
		const FGeometryCollection& InCollection,
		bool bComputeContact,
		TArray<FBoneAdjacencyEdge>& OutEdges);

	/**
	 * Bounds of all geometry-bearing bones in collection space, computed the same way the fracture backend
	 * computes its own: per-bone bounds transformed by the bone's global matrix.
	 */
	PCGUTILSFRACTURE_API FBox ComputeCollectionBounds(const FGeometryCollection& InCollection);
}
