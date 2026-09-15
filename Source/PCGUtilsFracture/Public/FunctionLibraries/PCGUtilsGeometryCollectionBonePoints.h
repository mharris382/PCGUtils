// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"

#include "PCGUtilsGeometryCollectionBonePoints.generated.h"

class FGeometryCollection;
class UPCGPointArrayData;
struct FPCGContext;

/**
 * Which per-bone attributes a bone-to-points conversion writes, and under what names.
 *
 * One toggle and one name per attribute, so the details panel is the list of what the conversion can produce
 * and nothing is written that was not asked for. Identity has no toggle - it is the contract with
 * Select Bones From Points, which cannot resolve a selection without it - but its names are still exposed so
 * they can be matched against that node and renamed to avoid collisions.
 *
 * Shared so that GC | Bones To Points and any node that converts bones to points internally produce exactly
 * the same points. A filter written against one must mean the same thing against the other.
 */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionBonePointAttributes
{
	GENERATED_BODY()

	// --- Identity -------------------------------------------------------------------------------------

	/** Bone index this point represents. Must match Select Bones From Points' Bone Index Attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	FName BoneIndexAttributeName = PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute;

	/** Identifies the collection lineage these bone indices came from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	FName SourceIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceIdAttribute;

	/** Revision number, for human-readable staleness diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	FName SourceRevisionAttributeName = PCGUtilsGeometryCollectionIdentity::SourceRevisionAttribute;

	/** Identifies the exact collection state; this is what makes a stale selection detectable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	FName SourceStateIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceStateIdAttribute;

	/**
	 * A per-bone id that survives reindexing, so a point can still name its bone after the collection has been
	 * fractured or pruned again.
	 *
	 * Bone Index answers "which bone in this exact state" and is rejected the moment the state changes, which
	 * is the right behaviour for a selection. This answers the different question "is this the same bone as
	 * before", and is what a multi-pass workflow needs. Off by default: only enable it if something downstream
	 * follows bones across revisions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	bool bOutputBoneId = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity",
		meta=(PCG_Overridable, EditCondition="bOutputBoneId", EditConditionHides))
	FName BoneIdAttributeName = PCGUtilsGeometryCollectionIdentity::BoneIdPointAttribute;

	// --- Hierarchy ------------------------------------------------------------------------------------

	/** Index of this bone's parent, or -1 at the root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy", meta=(PCG_Overridable))
	bool bOutputParentIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputParentIndex", EditConditionHides))
	FName ParentIndexAttributeName = PCGUtilsGeometryCollectionIdentity::ParentIndexAttribute;

	/** Depth below the root: 0 at the root, 1 for its children, and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy", meta=(PCG_Overridable))
	bool bOutputHierarchyLevel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputHierarchyLevel", EditConditionHides))
	FName HierarchyLevelAttributeName = PCGUtilsGeometryCollectionIdentity::HierarchyLevelAttribute;

	/** Index into the collection's geometry group, for cross-referencing raw collection data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy", meta=(PCG_Overridable))
	bool bOutputGeometryIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputGeometryIndex", EditConditionHides))
	FName GeometryIndexAttributeName = PCGUtilsGeometryCollectionIdentity::GeometryIndexAttribute;

	// --- Size -----------------------------------------------------------------------------------------

	/**
	 * Volume of this piece's bounding box. Not true mesh volume - the collection's real Volume attribute
	 * requires convex-hull generation, which is far too heavy for a points conversion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size", meta=(PCG_Overridable))
	bool bOutputBoundsVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
		meta=(PCG_Overridable, EditCondition="bOutputBoundsVolume", EditConditionHides))
	FName BoundsVolumeAttributeName = PCGUtilsGeometryCollectionIdentity::BoundsVolumeAttribute;

	// --- Surface --------------------------------------------------------------------------------------
	// The collection tracks an Internal flag per face, so each piece's surface splits into what it inherited
	// from the source mesh and what a fracture cut created. Enabling any of these costs one pass over the
	// collection's faces; leaving them all off skips that pass entirely.
	//
	// Before any fracture every face came from the source mesh, so every bone reads as fully exterior. The
	// breakdown only becomes interesting after the first cut.

	/**
	 * True when the piece has at least one face from the original mesh surface.
	 *
	 * This is what makes random damage safe. A piece with no exterior surface is buried inside the solid:
	 * pruning it changes nothing visible while leaving behind interior faces nothing will ever see.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputIsExterior = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputIsExterior", EditConditionHides))
	FName IsExteriorAttributeName = PCGUtilsGeometryCollectionIdentity::IsExteriorAttribute;

	/**
	 * Fraction of this piece's surface that was originally on the outside, in [0,1].
	 *
	 * Usually a better choice than raw area for selecting pieces, because it is scale-invariant: a threshold
	 * that works on one mesh works on the next. 0 is fully buried, near 1 is a piece the fracture barely
	 * touched, and the middle is a chunk with real exposure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputExposureRatio = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExposureRatio", EditConditionHides))
	FName ExposureRatioAttributeName = PCGUtilsGeometryCollectionIdentity::ExposureRatioAttribute;

	/** Surface area inherited from the original mesh, measured in collection space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputExteriorArea = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExteriorArea", EditConditionHides))
	FName ExteriorAreaAttributeName = PCGUtilsGeometryCollectionIdentity::ExteriorAreaAttribute;

	/** Surface area created by fracture cuts, measured in collection space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputInteriorArea = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputInteriorArea", EditConditionHides))
	FName InteriorAreaAttributeName = PCGUtilsGeometryCollectionIdentity::InteriorAreaAttribute;

	/** Number of faces inherited from the original mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputExteriorFaceCount = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExteriorFaceCount", EditConditionHides))
	FName ExteriorFaceCountAttributeName = PCGUtilsGeometryCollectionIdentity::ExteriorFaceCountAttribute;

	/** Number of faces created by fracture cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable))
	bool bOutputInteriorFaceCount = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(PCG_Overridable, EditCondition="bOutputInteriorFaceCount", EditConditionHides))
	FName InteriorFaceCountAttributeName = PCGUtilsGeometryCollectionIdentity::InteriorFaceCountAttribute;

	/** True when any surface attribute is enabled, so the per-face pass can be skipped entirely otherwise. */
	bool NeedsSurfaceInfo() const
	{
		return bOutputIsExterior || bOutputExposureRatio || bOutputExteriorArea
			|| bOutputInteriorArea || bOutputExteriorFaceCount || bOutputInteriorFaceCount;
	}
};

/**
 * The one bone-to-points conversion.
 *
 * A fracture piece becomes a point at the piece's centre, carrying the piece's orientation, scale and
 * point-local bounds, plus whichever per-bone attributes were asked for. Anything that wants to reason about
 * bones with ordinary point logic - GC | Bones To Points, or a node that runs point filters over bones
 * without ever emitting the points - goes through here, so a predicate cannot mean two different things.
 */
namespace PCGUtilsGeometryCollectionBonePoints
{
	/**
	 * The bones a conversion covers.
	 *
	 * @param bIncludeClusterBones  Also emit structural cluster/root transforms. Off is the useful default:
	 *                              only geometry-bearing leaf bones represent an actual fracture piece, and
	 *                              cluster bones would double-count regions during spatial filtering.
	 */
	PCGUTILSFRACTURE_API void GatherBones(
		const FGeometryCollection& InCollection, bool bIncludeClusterBones, TArray<int32>& OutBones);

	/**
	 * Builds one point per entry in InBones.
	 *
	 * @param InLocalToWorld        Applied to every bone's global transform. Identity leaves the points in
	 *                              collection space; pass the target actor transform for world space.
	 * @param OutGlobalTransforms   Optional: the collection's global bone matrices, which the caller would
	 *                              otherwise have to recompute to place anything alongside the points.
	 * @return nullptr if an enabled attribute had no name (already logged as a graph error).
	 */
	PCGUTILSFRACTURE_API UPCGPointArrayData* Build(
		FPCGContext* InContext,
		const UPCGGeometryCollectionData& InCollectionData,
		TConstArrayView<int32> InBones,
		const FTransform& InLocalToWorld,
		const FPCGUtilsGeometryCollectionBonePointAttributes& InAttributes,
		const FText& InNodeNameForMessages,
		TArray<FTransform>* OutGlobalTransforms = nullptr);
}
