// Copyright Max Harris

#pragma once

#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/MeshModelingFunctions.h"

#include "PCGDynMeshExtrude.generated.h"

/**
 * Linear extrusion with explicit cap/border result tracking, for realized meshes and deferred Builders.
 *
 * Distance is an input shorthand: a constant, or one value per input read from that input's @Data domain (a
 * path's PathHeight, say). The Geometry Script options this node used to hold as one struct are flattened into
 * individual settings; their override pins keep the labels they had. Assets saved before
 * FPCGUtilsCustomVersion::InputShorthands are migrated in PostLoad.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector extrude faces"))
class PCGUTILSDYNMESH_API UPCGDynMeshExtrudeSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshExtrudeSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExtrude"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	/**
	 * How far the faces move along the extrusion direction. Negative extrudes inward.
	 *
	 * @Data mode reads one value per input from the input's @Data domain: a number (Integer32, Integer64, Float or
	 * Double), for example PathHeight carried over from a path. The @Data. prefix is optional on DynMesh data. The
	 * attribute is required on every input: an input without it, or with a non-numeric value, fails with an error
	 * and is skipped; the constant is never used as a fallback. Builder inputs are refused in @Data mode, because
	 * a Builder has no @Data until it is realized.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDouble Distance;

	/** Fixed Direction, or the area-weighted average normal of each extruded area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	EGeometryScriptLinearExtrudeDirection DirectionMode = EGeometryScriptLinearExtrudeDirection::FixedDirection;

	/** Extrusion direction for Fixed Direction mode, and the fallback when an average normal is degenerate. Mesh-local. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	FVector Direction = FVector(0, 0, 1);

	/** Extrude the selection as one region, or each connected area / polygon separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	EGeometryScriptPolyOperationArea AreaMode = EGeometryScriptPolyOperationArea::EntireSelection;

	/** PolyGroup assignment for the cap faces. Shared result grouping, when enabled, overrides this for the result region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	FGeometryScriptMeshEditPolygroupOptions GroupOptions;

	/** UV scale applied to the new side faces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	float UVScale = 1.0f;

	/** Extruding a whole closed component offsets it as a solid shell rather than opening it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	bool bSolidsToShells = true;

	/** Faces retained as the active selection, and used for result PolyGroup assignment and the Result Selector. */
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
	FGeometryScriptMeshLinearExtrudeOptions Options_DEPRECATED;
};
