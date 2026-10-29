// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "CurveOps/TriangulateCurvesOp.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshPathToMesh.generated.h"

/**
 * Fills each closed path (or spline) with a triangulated DynMesh, optionally extruded into a solid.
 *
 * A replacement for the engine's Spline To Mesh that:
 *   - takes paths (point data marked closed by a @Data Boolean) directly, with no Create Spline step;
 *   - with Input Splines on, takes splines and 2D polygons exactly like the engine node;
 *   - copies the input's @Data attributes onto the output mesh;
 *   - frames the mesh at the path centroid, or at a transform the input carries in @Data;
 *   - can make the faces point toward an up direction whatever way the path winds.
 *
 * It asks nothing of its input that the engine node does not: an open path or spline is treated as closed by
 * default (Open Curves, as in the engine node), and the @Data closed flag on a path is optional.
 *
 * Coordinate space matches the engine node: vertices are stored relative to the mesh frame, and the frame
 * (a world transform) is written to the @Data Mesh Transform attribute. This is a creation node; it has no
 * DynMesh input, so there is no selection to honour.
 *
 * Exposed as two palette entries: DynMesh | Path To Mesh and DynMesh | Spline To Mesh.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|DynMesh|Creation",
	meta = (Keywords = "DynMesh mesh path paths spline splines polygon closed fill cap floor triangulate extrude create"))
class PCGUTILSDYNMESH_API UPCGDynMeshPathToMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPathToMeshSettings();

#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshCreation);
	}
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;

	/** Gives the shorthand members their plain override labels (Thickness, ThicknessAttribute, ...). */
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	// ── Input ─────────────────────────────────────────────────────────────────

	/**
	 * Off: the In pin takes paths (point data, points in path order). On: it takes splines and 2D polygons, like
	 * the engine's Spline To Mesh. Not overridable, because it changes the In pin's type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInputSplines = false;

	/**
	 * Optional, paths only. A @Data Boolean that marks a path as closed or open (the PCGEx convention). A path
	 * without it, or with this name empty, counts as closed. An open path follows Open Curves. The @Data. prefix
	 * is optional. Splines report their own closed state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input",
		meta = (PCG_Overridable, EditCondition = "!bInputSplines", EditConditionHides))
	FName ClosedAttributeName = TEXT("IsClosed");

	/** Splines only. How far the triangulated outline may stray from the spline curve before more vertices are added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input",
		meta = (PCG_Overridable, ClampMin = 0.001, EditCondition = "bInputSplines", EditConditionHides))
	double ErrorTolerance = 1.0;

	// ── Triangulation ─────────────────────────────────────────────────────────

	/** Whether and how to flatten the outline before filling it. Offsetting needs a flattened outline. 2D polygons always use Best Fit Plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation", meta = (PCG_Overridable))
	EFlattenCurveMethod FlattenMethod = EFlattenCurveMethod::DoNotFlatten;

	/** Grows (positive) or shrinks (negative) the outline before filling it. Needs a Flatten Method. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation",
		meta = (PCG_Overridable, EditCondition = "FlattenMethod != EFlattenCurveMethod::DoNotFlatten", EditConditionHides))
	double CurveOffset = 0.0;

	/** Outer Side grows or shrinks the filled shape; Both Sides makes a band of Curve Offset width along the outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation",
		meta = (PCG_Overridable, EditCondition = "FlattenMethod != EFlattenCurveMethod::DoNotFlatten && CurveOffset != 0", EditConditionHides))
	EOffsetClosedCurvesMethod OffsetClosedCurves = EOffsetClosedCurvesMethod::OffsetOuterSide;

	/**
	 * What to do with an open path or spline. Treat As Closed (the default, as in the engine node) joins its ends
	 * and fills it. Offset makes a band of Curve Offset width along it instead, and needs a Flatten Method and a
	 * Curve Offset; without them the input is treated as closed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation", meta = (PCG_Overridable))
	EOffsetOpenCurvesMethod OpenCurves = EOffsetOpenCurvesMethod::TreatAsClosed;

	/** The shape of the ends of an offset open curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation",
		meta = (PCG_Overridable, EditCondition = "FlattenMethod != EFlattenCurveMethod::DoNotFlatten && OpenCurves != EOffsetOpenCurvesMethod::TreatAsClosed && CurveOffset != 0", EditConditionHides))
	EOpenCurveEndShapes EndShapes = EOpenCurveEndShapes::Square;

	/** The shape of the corners of an offset outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation",
		meta = (PCG_Overridable, EditCondition = "FlattenMethod != EFlattenCurveMethod::DoNotFlatten && CurveOffset != 0", EditConditionHides))
	EOffsetJoinMethod JoinMethod = EOffsetJoinMethod::Square;

	/** How far a miter corner may reach before it is squared off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangulation",
		meta = (PCG_Overridable, ClampMin = 0.0, EditCondition = "FlattenMethod != EFlattenCurveMethod::DoNotFlatten && CurveOffset != 0 && JoinMethod == EOffsetJoinMethod::Miter", EditConditionHides))
	double MiterLimit = 1.0;

	// ── Facing ────────────────────────────────────────────────────────────────

	/**
	 * Turn the faces so they point toward Up Direction, whichever way the path winds. Off: faces follow the
	 * winding (counter-clockwise seen from above faces up), as in the engine node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing", meta = (PCG_Overridable))
	bool bFaceUpDirection = true;

	/**
	 * World-space direction the faces should point toward. In @Data mode, a required @Data Vector attribute on
	 * each input (default @Data.Normal); a missing or non-Vector attribute fails that input, the constant is not
	 * a fallback. Its length does not matter, but it must not be zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing",
		meta = (PCG_Overridable, EditCondition = "bFaceUpDirection", EditConditionHides))
	FPCGUtilsInputShorthandVector UpDirection;

	/** Reverses the faces. With Face Up Direction on, this makes them face away from Up Direction instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing", meta = (PCG_Overridable))
	bool bFlipResult = false;

	// ── Extrusion ─────────────────────────────────────────────────────────────

	/**
	 * Above 0, extrudes the fill into a solid this thick, toward the side the faces point. In @Data mode, a
	 * required numeric @Data attribute on each input (default @Data.Thickness, must be 0 or more); a missing or
	 * non-numeric attribute fails that input, the constant is not a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extrusion", meta = (PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs Thickness;

	/** Extrusion direction. Left at +Z with Extrude In Local Space on, the fill extrudes straight along its normals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extrusion", meta = (PCG_Overridable))
	FVector ExtrusionVector = FVector::ZAxisVector;

	/** On: Extrusion Vector is relative to each vertex normal (+Z is along the normal). Off: it is a world direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extrusion", meta = (PCG_Overridable))
	bool bExtrudeInLocalSpace = true;

	// ── Mesh Transform ────────────────────────────────────────────────────────

	/**
	 * Frame the mesh with a transform read from the input's @Data, when the input carries it. Takes priority over
	 * Use Centroid. An input without the attribute falls back to the next option; one with it as a non-Transform
	 * type falls back too, with a warning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Transform", meta = (PCG_Overridable))
	bool bUseTransformAttribute = true;

	/**
	 * Optional. The @Data Transform attribute to frame the mesh with, for example the component transform written
	 * by Get Component Data. The @Data. prefix is optional.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Transform",
		meta = (PCG_Overridable, EditCondition = "bUseTransformAttribute", EditConditionHides))
	FName TransformAttributeName = TEXT("RelativeTransform");

	/**
	 * Frame the mesh at the average of the path's points (a spline's control points), with no rotation or scale.
	 * Off: a spline or polygon uses its own transform, as in the engine node, and a path uses the world origin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Transform", meta = (PCG_Overridable))
	bool bUseCentroid = true;

	/**
	 * The @Data Transform attribute written on every output: the frame the mesh vertices are relative to, in
	 * world space. Always written, because the mesh cannot be placed without it; MeshTransform matches the
	 * engine's Spline To Mesh, so downstream nodes keep working.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Transform", meta = (PCG_Overridable))
	FName MeshTransformAttributeName = TEXT("MeshTransform");

	// ── Data ──────────────────────────────────────────────────────────────────

	/** Copy every @Data attribute of the input onto its output mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data", meta = (PCG_Overridable))
	bool bCopyDataAttributes = true;

	/** Paths only. Also copy the Closed Attribute. Off by default: a filled mesh is not a path, so the flag means nothing on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data",
		meta = (PCG_Overridable, EditCondition = "bCopyDataAttributes && !bInputSplines", EditConditionHides))
	bool bCopyClosedAttribute = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshPathToMeshElement : public IPCGDynamicMeshBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
