// Copyright Max Harris
#pragma once
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Geometry/PCGUtilsDynMeshRepair.h"
#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "PCGDynMeshRepairMesh.generated.h"

/** Local repair of invalid geometry; never commits a patch that creates a new boundary or intersection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh Repair Mesh Clean Intersections Coplanar Overlap Degenerate Sliver Planar"))
class PCGUTILSDYNMESH_API UPCGDynMeshRepairMeshSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshRepairMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Repair", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshIssueChecks Checks;
	/** Maximum mesh-local distance from the patch plane, including interior vertices. @Data requires a number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Repair", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs PlanarityTolerance = FPCGUtilsInputShorthandDoubleAbs(0.1, TEXT("RepairPlanarityTolerance"));
	/** Try this many neighbouring rings to find a simple patch boundary. Never expands beyond the input selection. @Data requires an integer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Repair", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandInteger32Abs MaximumExpansionRings = FPCGUtilsInputShorthandInteger32Abs(2, TEXT("RepairExpansionRings"));
	/** Maximum faces replaced by one repair. @Data requires a positive integer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Repair", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandInteger32Abs MaximumPatchTriangles = FPCGUtilsInputShorthandInteger32Abs(256, TEXT("RepairMaximumPatchTriangles"));
	/** Refuse patches with internal UV, normal, tangent or color seams. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preserve", meta=(PCG_Overridable))
	bool bPreserveAttributeSeams = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preserve", meta=(PCG_Overridable))
	bool bPreserveMaterials = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preserve", meta=(PCG_Overridable))
	bool bPreservePolygroups = true;
	/** Optional removal of redundant planar topology, using the existing planar dissolve algorithm. Can affect the whole input selection; off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar Cleanup", meta=(PCG_Overridable))
	bool bPlanarCleanup = false;
	/** Maximum angle in degrees for planar dissolve. @Data requires a number in [0,180]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar Cleanup", meta=(PCG_Overridable, EditCondition="bPlanarCleanup", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs PlanarAngle = FPCGUtilsInputShorthandDoubleAbs(0.1, TEXT("RepairPlanarAngle"));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Properties", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties OutputProperties;
	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &OutputProperties; }
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& Type) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;
protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshRepairMeshOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FPCGUtilsDynMeshIssueChecks Checks;
	FPCGUtilsInputShorthandDoubleAbs PlanarityTolerance, PlanarAngle;
	FPCGUtilsInputShorthandInteger32Abs MaximumExpansionRings, MaximumPatchTriangles;
	bool bPreserveAttributeSeams = true, bPreserveMaterials = true, bPreservePolygroups = true, bPlanarCleanup = false;
	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation, FPCGUtilsDynMeshProcessOutcome& Outcome) const override;
};
