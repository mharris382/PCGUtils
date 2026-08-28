#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "MeshTarget/PCGUtilsMeshTargetTypes.h"
#include "Data/PCGSplineStruct.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGSplineDeform.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsSplineDeformMappingMode : uint8
{
	/** Preserves the source mesh's longitudinal size in Unreal units; see Anchor and Distance Offset. */
	PreserveLength,
	/** Stretches/compresses the source mesh's existing topology to exactly span the spline range. */
	FitToSpline
};

UENUM(BlueprintType)
enum class EPCGUtilsSplineDeformAnchor : uint8
{
	/** The source mesh's minimum extent along Forward Axis maps to the start of the spline range. */
	Start,
	/** The source mesh bounds center maps to the center of the spline range. */
	Center,
	/** The source mesh's maximum extent along Forward Axis maps to the end of the spline range. */
	End
};

UENUM(BlueprintType)
enum class EPCGUtilsSplineDeformRangeMode : uint8
{
	/** The full spline, from distance 0 to its total length. */
	EntireSpline,
	/** An explicit [Start Distance, End Distance] sub-range of the spline. */
	DistanceRange
};

UENUM(BlueprintType)
enum class EPCGUtilsSplineDeformOutOfRangeMode : uint8
{
	/** Extends the centerline in a straight line along the endpoint's tangent for any excess distance. */
	ExtendAlongTangent,
	/** Clamps the mapped distance to the spline range; out-of-range vertices share the endpoint frame. */
	Clamp,
	/** Wraps the mapped distance periodically over the spline length. Requires Entire Spline + a closed spline. */
	Wrap
};

/**
 * Maps Dynamic Mesh vertex positions onto a PCG spline - a PCG-native analogue of Blender's Curve modifier.
 *
 * Unlike Blender's Curve modifier, the source mesh does not need to be pre-positioned or aligned with the
 * spline: the mapping frame (longitudinal axis + transverse centerline) is derived automatically from the full
 * source mesh's own bounding box, so a mesh authored anywhere still maps predictably onto the spline.
 *
 * This node changes vertex positions only. It never changes mesh topology: vertex count, triangle count, UV
 * topology, material IDs, PolyGroups, and vertex colors are always preserved exactly. To get a smoother bend,
 * add subdivisions upstream (eg via Remesh) before this node - Spline Deform will not add them for you.
 *
 * The Mesh input accepts either a whole Dynamic Mesh or a PCGUtilsDynMesh Mesh Selection, via the shared
 * FPCGUtilsMeshTargetHandle infrastructure (see MeshTarget/PCGUtilsMeshTargetFunctions.h), using the
 * FullMeshCopy preparation since this is a purely positional, topology-preserving operation. When the input is
 * a Mesh Selection, only the selected/blended vertex positions are restored into the result - but the mapping
 * frame is still derived from the complete source mesh bounds, not the selection, so narrowing the selection
 * never changes where the mesh maps onto the spline.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGSplineDeformSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SplineDeform"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection",
		meta = (DisplayName = "Selection Restoration", PCG_Overridable))
	FPCGUtilsSelectionBlendOptions SelectionBlend;

	// --- Deformation ---

	/** Which source mesh axis is longitudinal (mapped along the spline). The other two axes become the cross-section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (PCG_Overridable))
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (PCG_Overridable))
	bool bReverseDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (PCG_Overridable))
	EPCGUtilsSplineDeformMappingMode MappingMode = EPCGUtilsSplineDeformMappingMode::FitToSpline;

	/** Which part of the source mesh's longitudinal extent anchors to the spline range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation",
		meta = (PCG_Overridable, EditCondition = "MappingMode==EPCGUtilsSplineDeformMappingMode::PreserveLength", EditConditionHides))
	EPCGUtilsSplineDeformAnchor Anchor = EPCGUtilsSplineDeformAnchor::Center;

	/** Shifts the resulting mapping along the spline (in spline distance units) after the anchor/mapping calculation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (PCG_Overridable))
	float DistanceOffset = 0.0f;

	// --- Spline Range ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Range", meta = (PCG_Overridable))
	EPCGUtilsSplineDeformRangeMode RangeMode = EPCGUtilsSplineDeformRangeMode::EntireSpline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Range",
		meta = (PCG_Overridable, EditCondition = "RangeMode==EPCGUtilsSplineDeformRangeMode::DistanceRange", EditConditionHides))
	float StartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Range",
		meta = (PCG_Overridable, EditCondition = "RangeMode==EPCGUtilsSplineDeformRangeMode::DistanceRange", EditConditionHides))
	float EndDistance = 100.0f;

	// --- Spline ---

	/** If disabled (default), spline rotation/roll orients the cross section but spline scale does not affect its size. If enabled, the evaluated spline's transverse (Y/Z) scale also scales the cross-section offsets. Spline scale never affects distance along the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (PCG_Overridable))
	bool bUseSplineScale = false;

	/** Converts the spline's world-space evaluation into the PCG target actor's local space before deforming, matching the coordinate space Dynamic Mesh data is expected to be in (see the existing Spline To Dynamic Mesh node). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (PCG_Overridable))
	bool bConvertSplineToLocalSpace = true;

	// --- Out Of Range ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Out Of Range", meta = (PCG_Overridable))
	EPCGUtilsSplineDeformOutOfRangeMode OutOfRangeMode = EPCGUtilsSplineDeformOutOfRangeMode::ExtendAlongTangent;

	// --- Attributes ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (PCG_Overridable))
	bool bRecomputeNormals = true;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Spline deformation only moves vertices, so a Builder's active selection survives it. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FName GetMainInputPinLabel() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGSplineDeformElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	/** Resolving the target actor for spline local-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
};

/**
 * Bends a mesh along a spline.
 *
 * The spline arrives on a second PCG input pin, so it is resolved - and *copied by value* - in
 * CreateProcessOperation() while the Spline Deform node is executing. Copying matters: holding the
 * UPCGSplineData would be a hard UObject reference invisible to GC, and by evaluation time that data may be
 * long gone. FPCGSplineStruct is a plain value type, so the operation owns a self-contained snapshot.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSplineDeformOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	/** False when the spline could not be resolved at capture time; the operation then passes meshes through. */
	bool bIsValid = false;

	FPCGSplineStruct Spline;
	FTransform ActorTransform = FTransform::Identity;
	double SplineLength = 0.0;
	double EffectiveRangeStart = 0.0;
	double EffectiveRangeEnd = 0.0;
	EPCGUtilsSplineDeformOutOfRangeMode EffectiveOutOfRangeMode =
		EPCGUtilsSplineDeformOutOfRangeMode::ExtendAlongTangent;

	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;
	bool bReverseDirection = false;
	EPCGUtilsSplineDeformMappingMode MappingMode = EPCGUtilsSplineDeformMappingMode::FitToSpline;
	EPCGUtilsSplineDeformAnchor Anchor = EPCGUtilsSplineDeformAnchor::Center;
	float DistanceOffset = 0.0f;
	bool bUseSplineScale = false;
	bool bRecomputeNormals = true;
	FPCGUtilsSelectionBlendOptions SelectionBlend;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
