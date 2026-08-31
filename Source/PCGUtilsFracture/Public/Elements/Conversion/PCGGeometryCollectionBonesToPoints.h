// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGGeometryCollectionBonesToPoints.generated.h"

namespace PCGGeometryCollectionBonesToPointsConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName PointsOutputPin = TEXT("Points");

	/** Named to match the pin PCGEx cluster nodes expect on the other end. */
	inline const FName EdgesOutputPin = TEXT("Edges");
}

/**
 * Emits one PCG point per fracture piece so ordinary PCG/PCGEx spatial filtering can choose which bones an
 * operation should target. This node deliberately does no filtering of its own - that is what the rest of PCG
 * is for.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection Bones Chunks Fracture Pieces To Points GC"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionBonesToPointsSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCBonesToPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Emit points in the PCG target actor's world space rather than the collection's local space. Enable this
	 * when the points will be compared against world-space PCG geometry, which is the usual case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/**
	 * Also emit structural cluster/root transforms. Off by default: only geometry-bearing leaf bones represent
	 * an actual fracture piece, and cluster bones would double-count regions during spatial filtering.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones", AdvancedDisplay, meta=(PCG_Overridable))
	bool bIncludeClusterBones = false;

	// --- Identity -------------------------------------------------------------------------------------
	// Always written: these form the contract with Select Bones From Points, which cannot resolve a selection
	// without them. Their names are still exposed so they can be matched against that node and renamed to
	// avoid collisions.

	/** Bone index this point represents. Must match Select Bones From Points' Bone Index Attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity", meta=(PCG_Overridable))
	FName BoneIndexAttributeName = PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute;

	/** Identifies the collection lineage these bone indices came from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity", meta=(PCG_Overridable))
	FName SourceIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceIdAttribute;

	/** Revision number, for human-readable staleness diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity", meta=(PCG_Overridable))
	FName SourceRevisionAttributeName = PCGUtilsGeometryCollectionIdentity::SourceRevisionAttribute;

	/** Identifies the exact collection state; this is what makes a stale selection detectable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity", meta=(PCG_Overridable))
	FName SourceStateIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceStateIdAttribute;

	// --- Hierarchy ------------------------------------------------------------------------------------

	/** Index of this bone's parent, or -1 at the root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy", meta=(PCG_Overridable))
	bool bOutputParentIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputParentIndex", EditConditionHides))
	FName ParentIndexAttributeName = PCGUtilsGeometryCollectionIdentity::ParentIndexAttribute;

	/** Depth below the root: 0 at the root, 1 for its children, and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy", meta=(PCG_Overridable))
	bool bOutputHierarchyLevel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputHierarchyLevel", EditConditionHides))
	FName HierarchyLevelAttributeName = PCGUtilsGeometryCollectionIdentity::HierarchyLevelAttribute;

	/** Index into the collection's geometry group, for cross-referencing raw collection data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy", meta=(PCG_Overridable))
	bool bOutputGeometryIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition="bOutputGeometryIndex", EditConditionHides))
	FName GeometryIndexAttributeName = PCGUtilsGeometryCollectionIdentity::GeometryIndexAttribute;

	// --- Size -----------------------------------------------------------------------------------------

	/**
	 * Volume of this piece's bounding box. Not true mesh volume - the collection's real Volume attribute
	 * requires convex-hull generation, which is far too heavy for a points node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Size", meta=(PCG_Overridable))
	bool bOutputBoundsVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Size",
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputIsExterior = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputIsExterior", EditConditionHides))
	FName IsExteriorAttributeName = PCGUtilsGeometryCollectionIdentity::IsExteriorAttribute;

	/**
	 * Fraction of this piece's surface that was originally on the outside, in [0,1].
	 *
	 * Usually a better choice than raw area for selecting pieces, because it is scale-invariant: a threshold
	 * that works on one mesh works on the next. 0 is fully buried, near 1 is a piece the fracture barely
	 * touched, and the middle is a chunk with real exposure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputExposureRatio = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExposureRatio", EditConditionHides))
	FName ExposureRatioAttributeName = PCGUtilsGeometryCollectionIdentity::ExposureRatioAttribute;

	/** Surface area inherited from the original mesh, measured in collection space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputExteriorArea = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExteriorArea", EditConditionHides))
	FName ExteriorAreaAttributeName = PCGUtilsGeometryCollectionIdentity::ExteriorAreaAttribute;

	/** Surface area created by fracture cuts, measured in collection space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputInteriorArea = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputInteriorArea", EditConditionHides))
	FName InteriorAreaAttributeName = PCGUtilsGeometryCollectionIdentity::InteriorAreaAttribute;

	/** Number of faces inherited from the original mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputExteriorFaceCount = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputExteriorFaceCount", EditConditionHides))
	FName ExteriorFaceCountAttributeName = PCGUtilsGeometryCollectionIdentity::ExteriorFaceCountAttribute;

	/** Number of faces created by fracture cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
	bool bOutputInteriorFaceCount = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition="bOutputInteriorFaceCount", EditConditionHides))
	FName InteriorFaceCountAttributeName = PCGUtilsGeometryCollectionIdentity::InteriorFaceCountAttribute;

	// --- Cluster ---------------------------------------------------------------------------------------

	/**
	 * Also emit the bone adjacency graph, so the output reads as a cluster: which fracture pieces touch which.
	 *
	 * The Points output gains the vertex marking and an Edges pin appears beside it. Together they satisfy the
	 * convention PCGEx uses to recognise a cluster, which makes its whole cluster library - flood fill and its
	 * heuristics, pathfinding, refinement, connectivity filters - usable on a fractured Geometry Collection.
	 * Expanding a selection to neighbouring pieces is a flood fill from the selected vertices.
	 *
	 * The vertex id is the bone index, so a selection that has been through PCGEx cluster nodes still resolves
	 * through Select Bones From Points unchanged.
	 *
	 * Adjacency comes from the engine's precise proximity - touching vertices, or touching coplanar
	 * opposite-facing triangles - which is exactly what fracture cuts produce.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cluster", meta=(PCG_Overridable))
	bool bOutputCluster = false;

	/**
	 * Measure how much surface each pair of pieces shares, as edge attributes.
	 *
	 * Turns flood fill from "spread to neighbours" into "spread along strong joins first", which is much closer
	 * to how damage actually propagates. Requires generating convex hulls for every piece, so it is markedly
	 * slower than adjacency alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cluster",
		meta=(PCG_Overridable, EditCondition="bOutputCluster", EditConditionHides))
	bool bOutputContactArea = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cluster",
		meta=(PCG_Overridable, EditCondition="bOutputCluster && bOutputContactArea", EditConditionHides))
	FName ContactAreaAttributeName = TEXT("GC_ContactArea");

	/** Width of the contact where it is thin - separates a face-to-face join from a corner touch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cluster",
		meta=(PCG_Overridable, EditCondition="bOutputCluster", EditConditionHides))
	bool bOutputSharpContactWidth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cluster",
		meta=(PCG_Overridable, EditCondition="bOutputCluster && bOutputSharpContactWidth", EditConditionHides))
	FName SharpContactWidthAttributeName = TEXT("GC_SharpContactWidth");

	/** True when any edge attribute needs the (expensive) contact measurement. */
	bool NeedsContactInfo() const
	{
		return bOutputCluster && (bOutputContactArea || bOutputSharpContactWidth);
	}

	/** True when any surface attribute is enabled, so the per-face pass can be skipped entirely otherwise. */
	bool NeedsSurfaceInfo() const
	{
		return bOutputIsExterior || bOutputExposureRatio || bOutputExteriorArea
			|| bOutputInteriorArea || bOutputExteriorFaceCount || bOutputInteriorFaceCount;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionBonesToPointsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
