// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGDynMeshPlanarCut.generated.h"

/** Removes one side of a DynMesh or selected region with an infinite plane. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh Planar Plane Cut Trim Slice Bounds Alignment Padding"))
class PCGUTILSDYNMESH_API UPCGDynMeshPlanarCutSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

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

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* Context) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual void AddProcessOperationToCrc(FArchiveCrc32& Ar) const override;
};
