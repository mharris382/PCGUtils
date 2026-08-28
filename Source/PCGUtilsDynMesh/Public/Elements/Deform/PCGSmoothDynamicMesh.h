#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "MeshTarget/PCGUtilsMeshTargetTypes.h"

#include "PCGSmoothDynamicMesh.generated.h"

/**
 * Smoothing methods actually implementable against UE 5.8 Geometry Script / DynamicMesh APIs.
 *
 * GeometryScript's Deformations library (UGeometryScriptLibrary_MeshDeformFunctions) exposes exactly one
 * smoothing algorithm - ApplyIterativeSmoothingToMesh, a uniform one-ring-centroid Laplacian smoother. There is
 * no cotangent-weighted Laplacian and no Taubin/no-shrink variant exposed anywhere in Geometry Script or in the
 * public GeometryCore API, so those methods are not offered here as thin wrappers: a Cotangent option would
 * either be a duplicate of UniformLaplacian or require reimplementing cotangent-weight Laplacian smoothing from
 * scratch, which is out of scope for this element.
 */
UENUM(BlueprintType)
enum class EPCGUtilsDynamicMeshSmoothingMethod : uint8
{
	/** One-ring centroid Laplacian smoothing, delegated directly to GeometryScript's ApplyIterativeSmoothingToMesh. Simple and fast, but shrinks the mesh as iterations/strength increase. */
	UniformLaplacian,

	/** Alternating positive (Lambda) and negative (Mu) Laplacian passes. Not exposed by Geometry Script, so implemented as a small native helper (see ApplyTaubinSmoothingPass in the .cpp). Removes high-frequency noise/ridges while resisting the volume loss of plain Laplacian smoothing. */
	TaubinNoShrink
};

/**
 * Applies configurable smoothing to Dynamic Mesh data. Intended for softening geometric ridges left by boolean/
 * union operations, or for smoothing noisy/organic generated meshes, without collapsing the overall silhouette.
 *
 * Each input Dynamic Mesh is processed independently; inputs are never merged or unioned together.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGSmoothDynamicMeshSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SmoothDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Laplacian smoothing (uniform) shrinks meshes as iterations/strength increase. Taubin/no-shrink alternates an expansion and a contraction pass to remove noise while preserving overall mass - prefer it when the mesh's silhouette/volume matters, eg after a boolean union. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing", meta=(PCG_Overridable))
	EPCGUtilsDynamicMeshSmoothingMethod SmoothingMethod = EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink;

	/** Number of smoothing passes. Higher values smooth more aggressively (and, for Uniform Laplacian, shrink more). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing", meta=(PCG_Overridable, ClampMin="0", UIMin="0", UIMax="50"))
	int32 Iterations = 5;

	/** How far each vertex moves toward its neighborhood average per iteration. Ignored by Taubin No-Shrink, which uses Taubin Lambda/Mu instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0",
		EditCondition="SmoothingMethod==EPCGUtilsDynamicMeshSmoothingMethod::UniformLaplacian", EditConditionHides))
	float Strength = 0.25f;

	/** Locks or dampens smoothing along the mesh boundary (outer silhouette edges and any authored holes/openings, since both are open mesh boundaries). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Boundary", meta=(PCG_Overridable))
	bool bPreserveBoundaries = true;

	/** 0 = boundary vertices are fully locked in place. 1 = boundary vertices smooth exactly like interior vertices. Values in between blend the two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Boundary", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0",
		EditCondition="bPreserveBoundaries", EditConditionHides))
	float BoundarySmoothingWeight = 0.0f;

	/** Reserved for future use. Vertex-position smoothing moves every overlay element at a vertex together, so there is currently no meaningful distinction to make at UV seams; enabling this has no effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Boundary", meta=(PCG_Overridable))
	bool bPreserveUVSeams = false;

	/** Locks vertices touching an edge whose adjacent face normals differ by more than the angle threshold below. Off by default because the ridges this element is meant to soften (eg boolean/union seams) are themselves sharp edges - only enable this to protect other, intentionally-authored hard edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Boundary", meta=(PCG_Overridable))
	bool bPreserveSharpEdges = false;

	/** Edges whose adjacent triangle normals differ by more than this angle are treated as sharp/creased and locked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Boundary", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="180.0",
		EditCondition="bPreserveSharpEdges", EditConditionHides))
	float SharpEdgeAngleThresholdDegrees = 45.0f;

	/** If enabled, reads a per-vertex float weight map baked into the mesh (eg via a GeometryScript weight-map node upstream) to scale smoothing per-vertex: 0 = locked, 1 = full smoothing, values between blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Weight Attribute", meta=(PCG_Overridable))
	bool bUseSmoothWeightAttribute = false;

	/** Name of the Dynamic Mesh vertex weight-map layer to read. If the layer is missing, a warning is logged and smoothing proceeds as if this option were disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Weight Attribute", meta=(PCG_Overridable,
		EditCondition="bUseSmoothWeightAttribute", EditConditionHides))
	FName SmoothWeightAttributeName = "SmoothWeight";

	/** Recomputes smooth (area/angle-weighted) per-vertex normals after smoothing. This only affects shading; it does not remove geometric ridges by itself, so it is a shading cleanup step, not a substitute for smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing", meta=(PCG_Overridable))
	bool bRecomputeNormalsAfterSmoothing = true;

	/** Controls how selected vertex positions are blended back when the input is a selection or factory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsSelectionBlendOptions SelectionBlend;

	/** Taubin's positive/expansion pass factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Taubin", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0",
		EditCondition="SmoothingMethod==EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink", EditConditionHides))
	float TaubinLambda = 0.5f;

	/** Taubin's negative/contraction pass factor. Must stay negative and slightly larger in magnitude than Taubin Lambda to resist shrinkage; the default (-0.53) is the commonly-used no-shrink value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Taubin", meta=(PCG_Overridable, ClampMin="-1.0", ClampMax="0.0",
		EditCondition="SmoothingMethod==EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink", EditConditionHides))
	float TaubinMu = -0.53f;

	/** Logs input/output vertex & triangle counts and the effective settings used for each processed mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smoothing|Debug")
	bool bLogMeshStats = false;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Smoothing only moves vertices, so a Builder's active selection survives it. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGSmoothDynamicMeshElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

/**
 * Laplacian / Taubin smoothing with per-vertex locking, run through the Mesh Target layer so a selection is
 * blended back rather than hard-cut.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSmoothOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EPCGUtilsDynamicMeshSmoothingMethod SmoothingMethod = EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink;
	int32 Iterations = 5;
	float Strength = 0.25f;
	bool bPreserveBoundaries = true;
	float BoundarySmoothingWeight = 0.0f;
	bool bPreserveSharpEdges = false;
	float SharpEdgeAngleThresholdDegrees = 45.0f;
	bool bUseSmoothWeightAttribute = false;
	FName SmoothWeightAttributeName;
	bool bRecomputeNormalsAfterSmoothing = true;
	FPCGUtilsSelectionBlendOptions SelectionBlend;
	float TaubinLambda = 0.5f;
	float TaubinMu = -0.53f;
	bool bLogMeshStats = false;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
