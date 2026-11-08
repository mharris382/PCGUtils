// Copyright Max Harris

#pragma once

#include "CleaningOps/HoleFillOp.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"

#include "PCGDynMeshFillHoles.generated.h"

/**
 * Fills open boundary loops (holes) with new triangles, as Modeling Mode's Mesh > Fill Holes tool does. It runs the
 * same engine operator (FHoleFillOp), so every fill type behaves exactly as it does in the tool.
 *
 * Which holes: with no selection, every hole. With a Selection or Selector, a hole is filled when any of its
 * boundary edges is selected. Selections in another domain are converted to edges first, so selecting the faces
 * around a hole selects that hole. The new fill triangles are the result selection.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh fill holes hole cap close boundary loop open edges patch watertight"))
class PCGUTILSDYNMESH_API UPCGDynMeshFillHolesSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshFillHolesSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshFillHoles"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** How each hole is triangulated. Minimal matches the Modeling Mode default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fill", meta=(PCG_Overridable))
	EHoleFillOpFillType FillType = EHoleFillOpFillType::Minimal;

	/** Delete lone triangles (all three vertices touch only that triangle) instead of filling around them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fill", meta=(PCG_Overridable))
	bool bRemoveIsolatedTriangles = false;

	/** Fill three-edge holes with one triangle, matching neighbour UVs and colors, before using Fill Type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fill", meta=(PCG_Overridable))
	bool bQuickFillSmallHoles = false;

	/**
	 * Skip holes with more boundary edges than this. 0 fills holes of any size. Not in the Modeling Mode tool:
	 * useful on an open surface, where the outer rim is also a boundary loop and usually has the most edges.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fill", meta=(PCG_Overridable, ClampMin="0", UIMin="0"))
	int32 MaxHoleEdges = 0;

	/** Smooth only: when on, triangles outside the hole are never changed (often a lower-quality fill). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	bool bConstrainToHoleInterior = true;

	/** Smooth only: vertex rings outside the hole that may be remeshed. Ignored when Constrain To Hole Interior is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, ClampMin="0", UIMin="0",
			EditCondition="FillType == EHoleFillOpFillType::Smooth && !bConstrainToHoleInterior", EditConditionHides))
	int32 RemeshingExteriorRegionWidth = 2;

	/** Smooth only: vertex rings outside the hole included in smoothing. Controls smoothness across the hole's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, ClampMin="0", UIMin="0", EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	int32 SmoothingExteriorRegionWidth = 2;

	/** Smooth only: vertex rings inward from the hole's edge over which the smoothing constraint falls off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, ClampMin="0", UIMin="0", EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	int32 SmoothingInteriorRegionWidth = 2;

	/** Smooth only: how smooth the fill is. Not linear; larger values are smoother. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="1.0",
			EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	float InteriorSmoothness = 0.2f;

	/** Smooth only: relative triangle density of the fill. Target edge length is the hole's mean edge length divided by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, ClampMin="0.001", ClampMax="10.0", EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	double FillDensityScalar = 1.0;

	/** Smooth only: project to the original mesh while remeshing. Can be slow on large meshes with many holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Smooth Fill",
		meta=(PCG_Overridable, EditCondition="FillType == EHoleFillOpFillType::Smooth", EditConditionHides))
	bool bProjectDuringRemesh = false;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Edge;
		return true;
	}

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* Context) const override;
};
