#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGBevelEdges.generated.h"

/**
 * Bevels Dynamic Mesh data using GeometryScript's ApplyMeshBevelEdgeSelection. The Mesh input pin accepts either a
 * whole Dynamic Mesh (every edge is beveled) or a PCGUtilsDynMesh Mesh Selection (only the selected edges are
 * beveled), via the shared PCGUtilsDynMesh Mesh Target Handle infrastructure (see
 * MeshTarget/PCGUtilsMeshTargetFunctions.h) - the same infrastructure Remesh/Warp/Spline Deform are built on.
 * Does not output a selection - beveling changes mesh topology, so the input selection's element IDs are no
 * longer valid afterward.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGBevelEdgesSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("BevelEdges"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Distance each beveled edge is inset from its initial position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable, ClampMin="0.0"))
	float BevelDistance = 1.0f;

	/** Number of edge loops added along the bevel faces. 0 produces a single planar chamfer face per edge - eg for a golf ramp rather than a rounded decorative bevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable, ClampMin="0"))
	int32 Subdivisions = 0;

	/** Roundness of the bevel. Ignored when Subdivisions is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0",
		EditCondition="Subdivisions>0", EditConditionHides))
	float RoundWeight = 1.0f;

	/** If the faces on either side of a beveled edge share a Material ID, the beveled face inherits it instead of using Set Material ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable))
	bool bInferMaterialID = false;

	/** Material ID assigned to the new faces introduced by the bevel, unless Infer Material ID applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable, EditCondition="!bInferMaterialID", EditConditionHides))
	int32 SetMaterialID = 0;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGBevelEdgesElement : public IPCGDynamicMeshBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
