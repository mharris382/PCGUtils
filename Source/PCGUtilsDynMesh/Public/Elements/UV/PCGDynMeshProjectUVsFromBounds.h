// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshProjectUVsFromBounds.generated.h"

namespace PCGDynMeshProjectUVsFromBoundsConstants
{
	inline const FName BoundsPin = TEXT("Bounds");
}

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshBoundsProjection : uint8
{
	Planar UMETA(ToolTip="Project along one axis: the box's face across that axis maps to UV 0 to 1. Every triangle uses the same mapping, so a texture laid over the box lines up with the world."),
	Box UMETA(ToolTip="Each triangle projects along the axis its normal points closest to, onto that face of the box, which maps to 0 to 1. The three face mappings overlap in UV space, like triplanar mapping.")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectionAxis : uint8
{
	X UMETA(ToolTip="Project along X: U follows Y, V follows Z."),
	Y UMETA(ToolTip="Project along Y: U follows X, V follows Z."),
	Z UMETA(ToolTip="Project along Z (top down): U follows X, V follows Y.")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectionBounds : uint8
{
	EachInput UMETA(DisplayName="Each Input", ToolTip="The box around each input's projected triangles (its selection, when it has one)."),
	AllInputs UMETA(DisplayName="All Inputs", ToolTip="One box around every input's triangles, so meshes projected together share one texture."),
	BoundsPin UMETA(DisplayName="Bounds Pin", ToolTip="The bounds of the data on the Bounds pin: points (each point's bounds), a volume, or any spatial data. Place a box in the level and sample it to set the texture area by hand."),
	Custom UMETA(ToolTip="Custom Bounds, typed in.")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectionSpace : uint8
{
	World UMETA(ToolTip="Project world positions: the mesh is moved by its target actor's transform first. Bounds Pin and Custom Bounds are world space."),
	Mesh UMETA(DisplayName="Mesh Local", ToolTip="Project the mesh's own positions. Bounds Pin and Custom Bounds are read in the mesh's space.")
};

/**
 * Projects UVs so a box maps exactly onto UV 0 to 1: the texture-to-world mapping a render target, a baked distance
 * field or a compute pass needs to know where each texel lies. The box can come from each mesh, all meshes together,
 * the Bounds pin (sample a box placed in the level) or Custom Bounds, then be padded and made square.
 *
 * The box actually used is written to @Data (Projection Min / Max Attribute, world or mesh space as projected), so a
 * material or tool can turn UV back into a position: Position = Min + UV * (Max - Min) on the projected axes.
 *
 * DynMesh | Project UVs projects from points instead, with a scale in UV units per cm.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV project projection planar box bounds world texture render target mapping selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshProjectUVsFromBoundsSettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshProjectUVsFromBounds"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=0))
	EPCGUtilsDynMeshBoundsProjection Projection = EPCGUtilsDynMeshBoundsProjection::Planar;

	/** The axis Planar projects along. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=1,
		EditCondition="Projection==EPCGUtilsDynMeshBoundsProjection::Planar", EditConditionHides))
	EPCGUtilsDynMeshProjectionAxis Axis = EPCGUtilsDynMeshProjectionAxis::Z;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProjectionBounds BoundsSource = EPCGUtilsDynMeshProjectionBounds::EachInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(PCG_Overridable,
		EditCondition="BoundsSource==EPCGUtilsDynMeshProjectionBounds::Custom", EditConditionHides))
	FBox CustomBounds = FBox(FVector(-500.0), FVector(500.0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProjectionSpace Space = EPCGUtilsDynMeshProjectionSpace::World;

	/** Grows the box by this much on every side, in cm, before mapping. Leaves room around the mesh's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(PCG_Overridable, ClampMin=0, UIMin=0))
	double Padding = 0.0;

	/**
	 * Grow the box's shorter projected side, about its centre, to match the longer, so a square texture gets square
	 * texels. Off stretches the box to fill both UV axes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(PCG_Overridable, DisplayName="Square"))
	bool bSquare = false;

	/** V runs from the box's maximum to its minimum instead, for tools whose image rows start at the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable))
	bool bFlipV = false;

	/** Write the box used to @Data, so the texture-to-world mapping can be read downstream. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bWriteBoundsAttributes = true;

	/**
	 * Optional output. @Data attribute (Vector) receiving the minimum corner of the box mapped to UV 0, after Padding
	 * and Square, in the projection Space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, EditCondition="bWriteBoundsAttributes"))
	FName ProjectionMinAttribute = TEXT("UVProjectionMin");

	/** Optional output. @Data attribute (Vector) receiving the maximum corner of the box mapped to UV 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, EditCondition="bWriteBoundsAttributes"))
	FName ProjectionMaxAttribute = TEXT("UVProjectionMax");

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;

	/** All Inputs needs every mesh at once, which a Builder chain does not have. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return BoundsSource != EPCGUtilsDynMeshProjectionBounds::AllInputs; }
	virtual FText GetBuilderInputRejectionReason() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshProjectUVsFromBoundsElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProjectUVsFromBoundsOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	EPCGUtilsDynMeshBoundsProjection Projection = EPCGUtilsDynMeshBoundsProjection::Planar;
	EPCGUtilsDynMeshProjectionAxis Axis = EPCGUtilsDynMeshProjectionAxis::Z;
	EPCGUtilsDynMeshProjectionSpace Space = EPCGUtilsDynMeshProjectionSpace::World;
	/** Set unless the box is per input; computed once by the settings. */
	TOptional<FBox> SharedBounds;
	double Padding = 0.0;
	bool bSquare = false;
	bool bFlipV = false;
	bool bWriteBoundsAttributes = true;
	FName ProjectionMinAttribute;
	FName ProjectionMaxAttribute;

	/** Pads and squares Box as the settings ask. Public for tests. */
	static FBox FinalBox(FBox Box, double Padding, bool bSquare, EPCGUtilsDynMeshBoundsProjection Projection, EPCGUtilsDynMeshProjectionAxis Axis);

protected:
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
