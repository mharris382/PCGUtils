// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshOperandProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGDynMeshBoolean.generated.h"

namespace PCGDynMeshBooleanConstants
{
	/** Output pins used only while bSeparateOperandContributions is set; they replace the single "Out" pin. */
	inline const FName OutputContributionPinA = TEXT("Out A");
	inline const FName OutputContributionPinB = TEXT("Out B");
}

/** Whole-solid boolean. Partial mesh selections are intentionally unsupported. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology", HideCategories=(Selector), meta=(Keywords="DynMesh mesh boolean union subtract intersect csg"))
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

	/** Contribution separation reproduces the boolean directly and cannot defer to a Builder. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return !bSeparateOperandContributions; }

	virtual FString GetAdditionalTitleInformation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EGeometryScriptBooleanOperation BooleanOperation = EGeometryScriptBooleanOperation::Intersection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FGeometryScriptMeshBooleanOptions BooleanOperationOptions;

	/**
	 * Replace the single combined "Out" with "Out A" and "Out B", partitioning the one boolean result by which
	 * operand each triangle came from. "Out A" is the part of the result originating from InA (for Subtract, the
	 * surviving InA shell); "Out B" is the part originating from InB, including generated cut/cavity surfaces.
	 * Every result triangle - source triangles, subtraction walls, and post-boolean hole-fill triangles - is
	 * assigned to exactly one output. Provenance comes from the boolean itself, never from PolyGroups.
	 *
	 * This is a structural change to the node's pins, so it is not a runtime override. It requires concrete
	 * DynMesh inputs: Builder inputs are unsupported while it is enabled. Single-mesh operations (Trim*, New
	 * PolyGroup*) and a missing InB put the whole result on "Out A" and leave "Out B" empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	bool bSeparateOperandContributions = false;

	/** Group surviving operand faces (including subtraction cut faces) in one fresh default-layer PolyGroup per operand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="!bSeparateOperandContributions && (BooleanOperation == EGeometryScriptBooleanOperation::Union || BooleanOperation == EGeometryScriptBooleanOperation::Subtract)", EditConditionHides))
	bool bAssignOperandPolygroup = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable,
		EditCondition="!bSeparateOperandContributions && bAssignOperandPolygroup && (BooleanOperation == EGeometryScriptBooleanOperation::Union || BooleanOperation == EGeometryScriptBooleanOperation::Subtract)", EditConditionHides))
	int32 OperandPolygroup = 0;

	/** Self-union each operand on a private copy before the boolean. Upstream meshes are never modified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bSelfUnionOperand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, EditCondition="bSelfUnionOperand", EditConditionHides))
	FGeometryScriptMeshSelfUnionOptions OperandSelfUnionOptions;

protected:
	virtual FName GetMainOutputPinLabel() const override
	{
		return bSeparateOperandContributions
			? PCGDynMeshBooleanConstants::OutputContributionPinA
			: Super::GetMainOutputPinLabel();
	}
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};
