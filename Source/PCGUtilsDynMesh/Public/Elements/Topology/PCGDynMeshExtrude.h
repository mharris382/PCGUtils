// Copyright Max Harris

#pragma once

#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/MeshModelingFunctions.h"

#include "PCGDynMeshExtrude.generated.h"

/** Which way the extruded faces move. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshExtrudeDirection : uint8
{
	/** Every face moves along Direction. */
	FixedDirection UMETA(DisplayName="Fixed Direction"),
	/** Every face of an extruded area moves along that area's one area-weighted average normal. */
	AverageFaceNormal UMETA(DisplayName="Average Face Normal"),
	/**
	 * Every face moves Distance along its own normal, and the area stays connected: each vertex moves along the
	 * average of its selected faces' normals, lengthened so the faces around it each end up Distance away and
	 * parallel to where they were (a shell, or Blender's Shrink/Fatten). A box's top and sides go up and out by the
	 * same amount; Average Face Normal would push them all diagonally.
	 */
	FaceNormals UMETA(DisplayName="Per Face Normal")
};

/**
 * Linear extrusion with explicit cap/border result tracking, for realized meshes and deferred Builders.
 *
 * Distance is an input shorthand: a constant, or one value per input read from that input's @Data domain (a
 * path's PathHeight, say). The Geometry Script options this node used to hold as one struct are flattened into
 * individual settings; their override pins keep the labels they had. Assets saved before
 * FPCGUtilsCustomVersion::InputShorthands are migrated in PostLoad, as are those saved before
 * FPCGUtilsCustomVersion::ExtrudePerFaceNormal (when Direction Mode gained Per Face Normal).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector extrude faces normal normals shell thicken fatten"))
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

	/**
	 * Which way the faces move: all along Direction, all along each area's average normal, or each face along its
	 * own normal (Per Face Normal), with the area kept connected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable, DisplayName="Direction Mode"))
	EPCGUtilsDynMeshExtrudeDirection ExtrudeDirection = EPCGUtilsDynMeshExtrudeDirection::FixedDirection;

	/** Extrusion direction for Fixed Direction mode, and the fallback when an average normal is degenerate. Mesh-local. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude",
		meta=(PCG_Overridable, EditCondition="ExtrudeDirection != EPCGUtilsDynMeshExtrudeDirection::FaceNormals", EditConditionHides))
	FVector Direction = FVector(0, 0, 1);

	/**
	 * Per Face Normal: how far a vertex may be pushed, as a multiple of Distance, to keep the faces around it
	 * parallel. Where faces meet at a sharp angle the exact push grows without limit (a 90 degree edge needs 1.41,
	 * a 150 degree fold almost 4), so it is capped here; past the cap those faces end up nearer than Distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="1.0", EditCondition="ExtrudeDirection == EPCGUtilsDynMeshExtrudeDirection::FaceNormals", EditConditionHides))
	double MaxCornerStretch = 4.0;

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
	/**
	 * The direction setting before Per Face Normal existed, typed with Geometry Script's two-value enum. Read once
	 * in PostLoad into ExtrudeDirection, which shows under the same label so override wires reconnect.
	 */
	UPROPERTY()
	EGeometryScriptLinearExtrudeDirection DirectionMode = EGeometryScriptLinearExtrudeDirection::FixedDirection;

	/** The Geometry Script options struct this node held before Distance became a shorthand. Read once in PostLoad. */
	UPROPERTY()
	FGeometryScriptMeshLinearExtrudeOptions Options_DEPRECATED;
};
