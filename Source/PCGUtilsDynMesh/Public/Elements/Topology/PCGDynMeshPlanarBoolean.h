// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshOperandProcessBase.h"
#include "Details/PCGUtilsInputShorthands.h"

#include "PCGDynMeshPlanarBoolean.generated.h"

/** How the two meshes are brought onto one plane. */
UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanSpace : uint8
{
	/** Both meshes lie on one plane (within Plane Tolerance); booleans their areas on it. */
	Coplanar,
	/** The meshes need not be coplanar: A is cut along B's outline seen along Projection Direction. */
	Projected
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanOperation : uint8
{
	Union,
	Intersection,
	Difference UMETA(DisplayName="Difference (A - B)"),
	ExclusiveOr UMETA(DisplayName="Exclusive")
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanTrim : uint8
{
	/** Remove the part of A inside B's footprint. */
	RemoveInside,
	/** Keep only the part of A inside B's footprint. */
	KeepInside
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanPlane : uint8
{
	/** The area-weighted plane of InA: through its area centroid, along its mean face normal. */
	BestFitA UMETA(DisplayName="Best Fit (A)"),
	/** The mesh-space XY plane through InA's area centroid (normal +Z). */
	XYPlane UMETA(DisplayName="XY Plane"),
	/** Plane Origin and Plane Normal, each a constant or read from InA's @Data. */
	Explicit
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanOverlap : uint8
{
	A,
	B
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanTriangulation : uint8
{
	/** Rebuild cut areas with constrained Delaunay, keeping only edges that carry information (outlines, feature edges, creases). */
	Delaunay,
	/** Keep every edge of every cut source triangle: each output triangle lies inside one source triangle. */
	KeepSourceEdges
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanInheritance : uint8
{
	/** UVs, normals, colours and weights interpolated from the source triangle. */
	Interpolate,
	/** Each corner copies the nearest corner of its source triangle. */
	Nearest,
	/** Layers are kept, values are not inherited (zero UVs/colours/weights, normals from the geometry). */
	None
};

UENUM(BlueprintType)
enum class EPCGDynMeshPlanarBooleanWeld : uint8
{
	/** Welded seam vertices take B's position, so B stays exactly as authored. */
	KeepB UMETA(DisplayName="Keep B"),
	/** Welded seam vertices take A's position. */
	KeepA UMETA(DisplayName="Keep A")
};

/**
 * DynMesh | Planar Boolean: booleans surface meshes in 2D, without making solids first.
 *
 * Coplanar: union, intersection, difference or exclusive of two meshes lying on one plane. Projected: trims A
 * along B's outline seen along a direction (the cut follows A's own surface), optionally stitching B into the
 * hole as one watertight mesh with shared seam vertices.
 *
 * The output is InA, mutated: InA's PCG metadata, tags and material slots are kept, and every output triangle
 * takes its PolyGroup, PolyGroup layers and material from the source triangle it lies in; vertex attributes are
 * inherited per Vertex Attribute Inheritance. Whole-mesh operation: like DynMesh | Boolean, it takes no Selection
 * or Selector (partial application has no meaning for a boolean of two whole surfaces).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology", HideCategories=(Selector),
	meta=(Keywords="DynMesh mesh planar boolean 2d polygon clipper union intersect difference subtract exclusive xor trim cut stitch weld surface coplanar project"))
class PCGUTILSDYNMESH_API UPCGDynMeshPlanarBooleanSettings : public UPCGUtilsDynMeshOperandProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPlanarBooleanSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPlanarBoolean"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif
	virtual FString GetAdditionalTitleInformation() const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* Context) const override;

	/** A Builder has no @Data until realized: Builders work only while every shorthand this node reads is Constant. */
	virtual bool SupportsDeferredBuilderProcessing() const override;
	virtual FText GetBuilderInputRejectionReason() const override;

	/** Coplanar booleans two meshes on one plane; Projected cuts A along B's outline seen along a direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGDynMeshPlanarBooleanSpace Space = EPCGDynMeshPlanarBooleanSpace::Coplanar;

	/** Which areas the output keeps. Difference is A minus B. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar", EditConditionHides))
	EPCGDynMeshPlanarBooleanOperation Operation = EPCGDynMeshPlanarBooleanOperation::Union;

	/** Where A and B overlap (Union, Intersection), the output takes this input's triangles, attributes and surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar", EditConditionHides))
	EPCGDynMeshPlanarBooleanOverlap OverlapSource = EPCGDynMeshPlanarBooleanOverlap::A;

	/** Coplanar: the plane both meshes must lie on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar", EditConditionHides))
	EPCGDynMeshPlanarBooleanPlane PlaneSource = EPCGDynMeshPlanarBooleanPlane::BestFitA;

	/**
	 * Plane Source Explicit: a point on the plane, in mesh space. Constant, or a Vector attribute read from InA's
	 * @Data (default name PlaneOrigin). Required in @Data mode: a missing or non-Vector attribute is an error for
	 * that input; the constant is never a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar && PlaneSource == EPCGDynMeshPlanarBooleanPlane::Explicit", EditConditionHides))
	FPCGUtilsInputShorthandVector PlaneOrigin;

	/**
	 * Plane Source Explicit: the plane's normal, in mesh space (need not be unit length; must not be zero).
	 * Constant, or a Vector attribute read from InA's @Data (default name PlaneNormal). Required in @Data mode:
	 * a missing or non-Vector attribute is an error for that input; the constant is never a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar && PlaneSource == EPCGDynMeshPlanarBooleanPlane::Explicit", EditConditionHides))
	FPCGUtilsInputShorthandVector PlaneNormal;

	/**
	 * Coplanar: the largest distance any vertex of A or B may be from the plane. A vertex farther off fails the
	 * input with an error naming the vertex and its distance. Constant, or a number read from InA's @Data
	 * (default name PlaneTolerance; required in @Data mode).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Coplanar", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs PlaneTolerance;

	/**
	 * Projected: the direction both meshes are seen along, in mesh space (default straight down). Must not be zero.
	 * Constant, or a Vector attribute read from InA's @Data (default name ProjectionDirection; required in @Data mode).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Projected", EditConditionHides))
	FPCGUtilsInputShorthandVector ProjectionDirection;

	/** Projected: remove the part of A inside B's footprint, or keep only that part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Projected", EditConditionHides))
	EPCGDynMeshPlanarBooleanTrim TrimMode = EPCGDynMeshPlanarBooleanTrim::RemoveInside;

	/**
	 * Projected, Remove Inside: add B, whole and uncut, and join its open boundary to A's cut boundary so the
	 * result is one mesh with shared vertices on the seam. Where B's boundary lies on A within Seam Tolerance the
	 * seam is a weld; elsewhere the gap is reported and, with Fill Seam Gaps, closed with a vertical wall.
	 * B must be an open surface (no walls or skirts along its outline).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Projected && TrimMode == EPCGDynMeshPlanarBooleanTrim::RemoveInside", EditConditionHides))
	bool bStitch = false;

	/**
	 * Stitch: the largest height gap (along the projection) at which a seam vertex of B is welded to A's cut.
	 * Constant, or a number read from InA's @Data (default name SeamTolerance; required in @Data mode).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Projected && TrimMode == EPCGDynMeshPlanarBooleanTrim::RemoveInside && bStitch", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs SeamTolerance;

	/** Stitch: close a seam gap wider than Seam Tolerance with vertical wall triangles (a warning names each run). Off leaves it open and reports it as an error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="Space == EPCGDynMeshPlanarBooleanSpace::Projected && TrimMode == EPCGDynMeshPlanarBooleanTrim::RemoveInside && bStitch", EditConditionHides))
	bool bFillSeamGaps = true;

	/** Which input's position a welded seam vertex takes (Coplanar Union and Exclusive welds too). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGDynMeshPlanarBooleanWeld WeldPosition = EPCGDynMeshPlanarBooleanWeld::KeepB;

	/** How cut triangles are rebuilt. Triangles that are not cut are always kept exactly as they were. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attributes, meta=(PCG_Overridable))
	EPCGDynMeshPlanarBooleanTriangulation Triangulation = EPCGDynMeshPlanarBooleanTriangulation::Delaunay;

	/**
	 * How rebuilt corners get UVs, normals, colours and weight layers. PolyGroups, PolyGroup layers and material IDs
	 * always come from the source triangle. Interpolate (default) preserves the most; Nearest copies the nearest
	 * source corner; None keeps the layers without inheriting values.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attributes, meta=(PCG_Overridable))
	EPCGDynMeshPlanarBooleanInheritance VertexAttributeInheritance = EPCGDynMeshPlanarBooleanInheritance::Interpolate;

	/** Move B's PolyGroup IDs (the default groups and every group layer) past A's, so the two sets stay distinct. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attributes, meta=(PCG_Overridable))
	bool bOffsetOperandPolygroups = true;

	/**
	 * Add B's material slots to the output's slot list (A's slots first; a material A already has is shared) and
	 * remap B's material IDs to match. Off keeps B's material IDs as they are, which assumes B already uses A's slots.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attributes, meta=(PCG_Overridable))
	bool bMergeOperandMaterials = true;

	/** 2D distance under which two points are treated as one (seam vertices, outline crossings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category=Settings, meta=(PCG_Overridable, ClampMin="0.0000001", UIMin="0.0001"))
	double SnapTolerance = 0.001;

};
