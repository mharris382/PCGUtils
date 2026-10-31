// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshTransformUVs.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshUVPivot : uint8
{
	Origin UMETA(DisplayName="UV Origin", ToolTip="Scale and rotate about UV (0, 0)."),
	RegionCenter UMETA(DisplayName="Region Center", ToolTip="About the centre of the UV bounds of all the triangles being transformed."),
	IslandCenter UMETA(DisplayName="Each Island Center", ToolTip="Each UV island about the centre of its own UV bounds, so islands stay where they are."),
	Custom UMETA(ToolTip="About Custom Pivot.")
};

/**
 * Scales, rotates and moves UVs, in that order, about a pivot. A DynMesh Selection or Selector transforms only those
 * triangles' UVs; a UV they share with other triangles is split, not moved under them.
 *
 * Translation, Rotation and Scale are input shorthands: a constant, or one value per input from its @Data.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV transform move translate offset rotate scale tile tiling selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshTransformUVsSettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshTransformUVsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshTransformUVs"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	/**
	 * UV offset added last.
	 *
	 * @Data mode reads one Vector2 (or Vector, using X and Y) per input from its @Data domain (default UVTranslation).
	 * Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector2 Translation;

	/**
	 * Counter-clockwise rotation about the pivot, in degrees.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) per input from its @Data domain (default
	 * UVRotation). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDouble Rotation;

	/**
	 * Scale about the pivot, applied first. 2 tiles a texture twice.
	 *
	 * @Data mode reads one Vector2 per input from its @Data domain (default UVScale). Required on every input; the
	 * constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector2 Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	EPCGUtilsDynMeshUVPivot Pivot = EPCGUtilsDynMeshUVPivot::Origin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable,
		EditCondition="Pivot==EPCGUtilsDynMeshUVPivot::Custom", EditConditionHides))
	FVector2D CustomPivot = FVector2D(0.5, 0.5);

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;

	/** A Builder has no @Data until realized, so only constant settings run deferred. */
	virtual bool SupportsDeferredBuilderProcessing() const override
	{
		return Translation.IsConstant() && Rotation.IsConstant() && Scale.IsConstant();
	}

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshTransformUVsElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshTransformUVsOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	FPCGUtilsInputShorthandVector2 Translation;
	FPCGUtilsInputShorthandDouble Rotation;
	FPCGUtilsInputShorthandVector2 Scale;
	EPCGUtilsDynMeshUVPivot Pivot = EPCGUtilsDynMeshUVPivot::Origin;
	FVector2D CustomPivot = FVector2D(0.5, 0.5);

protected:
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
