// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "PCGDynMeshPlanarCut.generated.h"

/** Removes one side of a DynMesh or selected region with an infinite plane. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh Planar Plane Cut Trim Slice Bounds Alignment Padding"))
class PCGUTILSDYNMESH_API UPCGDynMeshPlanarCutSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** Mesh properties written to @Data after this node runs, as DynMesh | Properties writes them. All off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Properties", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties OutputProperties;

	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &OutputProperties; }

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPlanarCut"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Plane", meta=(PCG_Overridable))
	EPCGUtilsPlaneTransformMode TransformMode = EPCGUtilsPlaneTransformMode::Explicit;

	/** Cutting frame in DynMesh local space. Its Z axis is the plane normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Plane",
		meta=(PCG_Overridable, EditCondition="TransformMode == EPCGUtilsPlaneTransformMode::Explicit", EditConditionHides))
	FTransform PlaneTransform = FTransform::Identity;

	/** Places the plane origin relative to the target's padded local bounds; Local Transform sets final offset/orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Plane",
		meta=(PCG_Overridable, EditCondition="TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative", EditConditionHides, ShowOnlyInnerProperties))
	FPCGUtilsBoundsRelativeTransformDetails BoundsPlacement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cut", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptMeshPlaneCutOptions Options;

	/**
	 * Adds one Selector output carrying one tagged selector per enabled category, across all result meshes, as
	 * DynMesh | Boolean does. Filter by category tag before connecting to a process Selector input. Requires
	 * concrete DynMeshes: Builder inputs are unsupported while it is on. Not overridable: it changes the node's pins.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Selectors")
	bool bOutputSelectors = false;

	/**
	 * Select the cut itself: the edges the plane left on the mesh (the outline of the cross section), their
	 * vertices, and the faces touching them. In the Edge domain this is exactly the cross-section outline, ready
	 * for Selection To Paths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Selectors", meta=(PCG_Overridable, EditCondition="bOutputSelectors", EditConditionHides))
	bool bCutSelector = true;

	/** Tag added to each cut selector. None omits the category tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Selectors", meta=(PCG_Overridable, EditCondition="bOutputSelectors && bCutSelector", EditConditionHides))
	FName CutSelectorTag = TEXT("Cut");

	/**
	 * Select the cap: the faces that fill the cut, with their edges and vertices. Hidden, and not output, when
	 * Fill Holes is off, because the cut then has no cap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Selectors", meta=(PCG_Overridable, EditCondition="ShowFillSelector()", EditConditionHides))
	bool bFillSelector = true;

	/** Tag added to each fill selector. None omits the category tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Selectors", meta=(PCG_Overridable, EditCondition="ShowFillSelectorTag()", EditConditionHides))
	FName FillSelectorTag = TEXT("Fill");

	UFUNCTION()
	bool ShowFillSelector() const { return bOutputSelectors && Options.bFillHoles; }

	UFUNCTION()
	bool ShowFillSelectorTag() const { return ShowFillSelector() && bFillSelector; }

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* Context) const override;
	/** Result selectors name elements of a concrete mesh. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return !bOutputSelectors; }
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual void EmitAdditionalOutputs(FPCGContext* Context) const override;
	virtual FPCGElementPtr CreateElement() const override;
	virtual void AddProcessOperationToCrc(FArchiveCrc32& Ar) const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshPlanarCutElement final : public FPCGUtilsDynMeshProcessBaseElement
{
};
