// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshOperandProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGDynMeshBoolean.generated.h"

/** Whole-solid boolean. Partial mesh selections are intentionally unsupported. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology", HideCategories=(Selector))
class PCGUTILSDYNMESH_API UPCGDynMeshBooleanSettings : public UPCGUtilsDynMeshOperandProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshBoolean"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EGeometryScriptBooleanOperation BooleanOperation = EGeometryScriptBooleanOperation::Intersection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FGeometryScriptMeshBooleanOptions BooleanOperationOptions;

	/** Group surviving operand faces (including subtraction cut faces) in one fresh default-layer PolyGroup per operand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="BooleanOperation == EGeometryScriptBooleanOperation::Union || BooleanOperation == EGeometryScriptBooleanOperation::Subtract", EditConditionHides))
	bool bAssignOperandPolygroup = false;

	/** Self-union each operand on a private copy before the boolean. Upstream meshes are never modified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bSelfUnionOperand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, EditCondition="bSelfUnionOperand", EditConditionHides))
	FGeometryScriptMeshSelfUnionOptions OperandSelfUnionOptions;
};
