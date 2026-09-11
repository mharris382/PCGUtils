// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class FGeometryCollection;

/**
 * Small shared helpers for measuring Geometry Collection geometry. Deliberately thin: anything Epic already
 * implements (selection algorithms, prune, fracture) is called directly rather than wrapped.
 *
 * Hierarchy questions - what is a piece, a cluster, a root, what level is a bone at - live in
 * PCGUtilsGeometryCollectionHierarchy instead.
 */
namespace PCGUtilsGeometryCollectionHelpers
{
	/**
	 * Bone transforms resolved from parent-relative to collection space. Stored Transform[] is relative to the
	 * parent, so anything spatial must go through here - the same thing FFractureEngineFracturing does
	 * internally before computing its own bounds.
	 */
	PCGUTILSFRACTURE_API void ComputeGlobalTransforms(
		const FGeometryCollection& InCollection, TArray<FTransform>& OutGlobalTransforms);

	/**
	 * Places a whole collection under an additional transform, by composing it onto each root bone's *local*
	 * transform.
	 *
	 * Geometry is deliberately untouched. A bone transform belongs to the collection state rather than to its
	 * geometry, so placing a collection this way leaves every cached piece mesh valid, while rewriting vertices
	 * would invalidate the lot and fold the placement's scale irreversibly into the shape. Every spatial
	 * consumer here already resolves bone transforms through ComputeGlobalTransforms - ComputeCollectionBounds
	 * and GC | To DynMesh both do - so a non-identity root is honoured throughout.
	 *
	 * GlobalMatrices composes `Global = Local * ParentGlobal`, which is why the placement multiplies on the
	 * right: it has to act as the root's new parent.
	 */
	PCGUTILSFRACTURE_API void PlaceCollection(
		FGeometryCollection& InOutCollection, const FTransform& InPlacement);

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

	/** "bones: 57 (46 piece(s), 11 cluster(s)), faces: 12480, vertices: 6203" - one-line summary logging. */
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

	/**
	 * The bones touching the given ones, at the same depth in the hierarchy.
	 *
	 * Mirrors FGeometryCollectionProximityUtility::EnumerateNeighbors, which is what Fracture Mode's Contact
	 * button and the facade's SelectContact both use: proximity is only ever computed between pieces, so a
	 * cluster's neighbours are found by looking at the pieces beneath it and then walking each neighbour back
	 * *up* to the queried bone's level. Selecting a cluster's contacts therefore gives sibling clusters, not
	 * the pieces inside them.
	 *
	 * Implemented here rather than through the facade because the facade deep-copies the whole collection to
	 * compute proximity, while the module's collections are immutable and can use the const overload.
	 *
	 * @param bIncludeNeighborsInParentLevels  Also report a touching bone that sits nearer the root than the
	 *                                         queried bone, which would otherwise be skipped entirely.
	 * @param InIterations                     Spread this many steps. Proximity is computed once regardless,
	 *                                         which is the whole reason iteration lives here rather than in
	 *                                         the caller.
	 * @param OutBones                         The bones reached, excluding the queried ones unless they were
	 *                                         reached back through a neighbour. Sorted.
	 * @return false if proximity could not be determined; OutBones is then untouched.
	 */
	PCGUTILSFRACTURE_API bool GatherContactNeighbors(
		const FGeometryCollection& InCollection,
		TConstArrayView<int32> InBones,
		bool bIncludeNeighborsInParentLevels,
		int32 InIterations,
		TArray<int32>& OutBones);
}
