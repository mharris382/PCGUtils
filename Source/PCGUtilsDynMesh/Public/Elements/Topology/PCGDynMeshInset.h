// Copyright Max Harris

#pragma once

#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/MeshModelingFunctions.h"

#include "PCGDynMeshInset.generated.h"

/**
 * Inset/outset with the inner face region available for the next operation.
 *
 * Distance is an input shorthand: a constant, or one value per input read from that input's @Data domain. The
 * Geometry Script options this node used to hold as one struct are flattened into individual settings; their
 * override pins keep the labels they had. Assets saved before FPCGUtilsCustomVersion::InputShorthands are
 * migrated in PostLoad.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector inset faces"))
class PCGUTILSDYNMESH_API UPCGDynMeshInsetSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshInsetSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshInset"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	/**
	 * How far the border moves inward. Positive insets; negative outsets.
	 *
	 * @Data mode reads one value per input from the input's @Data domain: a number (Integer32, Integer64, Float or
	 * Double). The @Data. prefix is optional on DynMesh data. The attribute is required on every input: an input
	 * without it, or with a non-numeric value, fails with an error and is skipped; the constant is never used as a
	 * fallback. Builder inputs are refused in @Data mode, because a Builder has no @Data until it is realized.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDouble Distance;

	/** Project the inset border back onto the original surface. Inset only; ignored when outsetting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	bool bReproject = true;

	/** Move only the border vertices, leaving the region's interior vertices in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	bool bBoundaryOnly = false;

	/** Smooths the interior of the region after the border moves. 0 leaves it as is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	float Softness = 0.0f;

	/** Area correction applied while solving the interior. 1 keeps the default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	float AreaScale = 1.0f;

	/** Inset the selection as one region, or each connected area / polygon separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	EGeometryScriptPolyOperationArea AreaMode = EGeometryScriptPolyOperationArea::EntireSelection;

	/** PolyGroup assignment for the inner faces. Shared result grouping, when enabled, overrides this for the result region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	FGeometryScriptMeshEditPolygroupOptions GroupOptions;

	/** UV scale applied to the new border faces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	float UVScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

	/** A Builder carries no @Data until realized, so @Data-driven Distance only runs on realized meshes. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return Distance.IsConstant(); }
	virtual FText GetBuilderInputRejectionReason() const override;

	virtual void PostLoad() override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* Context) const override;

private:
	/** The Geometry Script options struct this node held before Distance became a shorthand. Read once in PostLoad. */
	UPROPERTY()
	FGeometryScriptMeshInsetOutsetFacesOptions Options_DEPRECATED;
};
