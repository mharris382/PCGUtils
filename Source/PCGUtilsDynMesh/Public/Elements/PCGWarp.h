#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/MeshDeformFunctions.h"
#include "PCGSettings.h"

#include "PCGWarp.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsWarpType : uint8
{
	/** UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh. */
	Bend,
	/** UGeometryScriptLibrary_MeshDeformFunctions::ApplyTwistWarpToMesh. */
	Twist,
	/** UGeometryScriptLibrary_MeshDeformFunctions::ApplyFlareWarpToMesh. Negative percentages squish inward. */
	Flare
};

UENUM(BlueprintType)
enum class EPCGUtilsWarpControlMode : uint8
{
	/** The Orientation property below defines a single, fixed warp frame. */
	SettingsTransform,
	/** Each connected Point Data input's first point defines one warp frame; multiple inputs warp sequentially. */
	PointData
};

/**
 * Applies a Geometry Script space-deformation warp (Bend, Twist, or Flare/Squish) to Dynamic Mesh data. The Mesh
 * input pin accepts either a whole Dynamic Mesh or a PCGUtilsDynMesh Mesh Selection - for a Selection, only the
 * positions of the selected vertices change; topology, UVs, normals, colors, materials, and PolyGroups are left
 * completely untouched, via the shared PCGUtilsDynMesh Mesh Target Handle infrastructure (see
 * MeshTarget/PCGUtilsMeshTargetFunctions.h).
 *
 * In Point Data control mode, each connected Point Data input contributes one warp "controller" from its first
 * point (Transform defines the warp pivot/axis; Density optionally scales strength; Bounds optionally define the
 * warp's lower/upper extent along the point's local Z axis) - multiple inputs apply sequentially to the same
 * target mesh before the result is restored once.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGWarpSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("Warp"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp", meta = (PCG_Overridable))
	EPCGUtilsWarpType WarpType = EPCGUtilsWarpType::Bend;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp", meta = (PCG_Overridable))
	EPCGUtilsWarpControlMode ControlMode = EPCGUtilsWarpControlMode::SettingsTransform;

	/** Fixed warp frame (pivot = location, warp axis = local +Z/-Z). Scale is not used - Geometry Script's warp frame is position+rotation only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Transform",
		meta = (PCG_Overridable, EditCondition = "ControlMode==EPCGUtilsWarpControlMode::SettingsTransform", EditConditionHides))
	FTransform Orientation = FTransform::Identity;

	/** If enabled, each control point's Bounds along its local Z axis define the warp's lower/upper extent, overriding the Lower Extent/Extent settings below (forcing asymmetric extents). Preferred over authoring extents manually. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Point Data",
		meta = (PCG_Overridable, EditCondition = "ControlMode==EPCGUtilsWarpControlMode::PointData", EditConditionHides))
	bool bInferExtentsFromPointBounds = true;

	/** If enabled, converts each control point's transform into the PCG target actor's local space before warping, matching the coordinate space Dynamic Mesh data is expected to be in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Point Data",
		meta = (PCG_Overridable, EditCondition = "ControlMode==EPCGUtilsWarpControlMode::PointData", EditConditionHides))
	bool bConvertControlPointsToLocalSpace = true;

	/** If enabled, scales the warp's angle/percent strength (not its extent) by each control point's Density: 1.0 = full configured strength, 0.0 = no effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Point Data",
		meta = (PCG_Overridable, EditCondition = "ControlMode==EPCGUtilsWarpControlMode::PointData", EditConditionHides))
	bool bScaleStrengthByPointDensity = false;

	// --- Bend ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bend", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Bend", EditConditionHides))
	float BendAngle = 45.0f;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bend", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Bend", EditConditionHides))
	float BendExtent = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bend", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Bend", EditConditionHides))
	bool bBendSymmetricExtents = true;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bend", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Bend && !bBendSymmetricExtents", EditConditionHides))
	float BendLowerExtent = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bend", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Bend", EditConditionHides))
	bool bBendBidirectional = true;

	// --- Twist ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twist", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Twist", EditConditionHides))
	float TwistAngle = 90.0f;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twist", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Twist", EditConditionHides))
	float TwistExtent = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twist", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Twist", EditConditionHides))
	bool bTwistSymmetricExtents = true;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twist", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Twist && !bTwistSymmetricExtents", EditConditionHides))
	float TwistLowerExtent = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twist", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Twist", EditConditionHides))
	bool bTwistBidirectional = true;

	// --- Flare ---

	/** Negative values squish inward instead of flaring outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Flare", EditConditionHides))
	float FlarePercentX = 50.0f;

	/** Negative values squish inward instead of flaring outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Flare", EditConditionHides))
	float FlarePercentY = 50.0f;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Flare", EditConditionHides))
	float FlareExtent = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Flare", EditConditionHides))
	bool bFlareSymmetricExtents = true;

	/** Ignored when Infer Extents From Point Bounds is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "WarpType==EPCGUtilsWarpType::Flare && !bFlareSymmetricExtents", EditConditionHides))
	float FlareLowerExtent = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (PCG_Overridable, EditCondition = "WarpType==EPCGUtilsWarpType::Flare", EditConditionHides))
	EGeometryScriptFlareType FlareType = EGeometryScriptFlareType::SinMode;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGWarpElement : public IPCGElement
{
public:
	/** Resolving the target actor for control-point local-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
