#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGSetVertexColor.generated.h"

/**
 * Sets Dynamic Mesh vertex colors using GeometryScript's SetMeshConstantVertexColor / SetMeshSelectionVertexColor.
 * The Mesh input pin accepts either a whole Dynamic Mesh (every vertex is set) or a PCGUtilsDynMesh Mesh Selection
 * (only the selected elements are set), via the shared PCGUtilsDynMesh Mesh Target Handle infrastructure (see
 * MeshTarget/PCGUtilsMeshTargetFunctions.h) - the same infrastructure Bevel Edges is built on, since
 * SetMeshSelectionVertexColor is itself selection-aware and needs no region extraction/weld.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGSetVertexColorSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SetVertexColor"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Which RGBA channels to set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptColorFlags ColorFlags;

	/** Only applies to a whole Dynamic Mesh input (SetMeshConstantVertexColor); ignored for a Mesh Selection input, which has no equivalent option. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bClearExisting = false;

	/** Only applies to a Mesh Selection input (SetMeshSelectionVertexColor). If true, a "hard edge" is created by giving every selected triangle its own color elements; Vertex selections are converted to Triangle selections and Color Flags is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bCreateColorSeam = false;

	/** If true, the color is read from a Data domain attribute on each input (see Color Attribute Name) instead of using Color below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bUseDataAttributeColor = false;

	/** Constant color to set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable,
		EditCondition="!bUseDataAttributeColor", EditConditionHides))
	FLinearColor Color = FLinearColor::White;

	/** Name of the Data domain (Vector4) attribute to read the color from, on each Mesh/Selection input. An error is raised if the attribute is missing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable,
		EditCondition="bUseDataAttributeColor", EditConditionHides))
	FName ColorAttributeName = "VertexColor";

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/**
	 * Setting vertex colors never changes topology, so a Builder can be decorated with it - except in
	 * attribute mode, where the colour is read from a Data-domain attribute on the *incoming PCG data*. A
	 * Builder subtree's result is freshly created geometry and carries no such attribute, so that mode stays
	 * immediate-only rather than silently resolving to the fallback colour.
	 */
	virtual bool SupportsDeferredBuilderProcessing() const override { return !bUseDataAttributeColor; }

#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

protected:
	virtual FName GetMainInputPinLabel() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGSetVertexColorElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

/**
 * Sets vertex colours on a whole mesh or on just the effective selection. Both Geometry Script entry points
 * are selection-aware in their own right, so no Mesh Target working copy is needed - the operation writes
 * straight into the mesh it was handed, which is already private to the caller.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSetVertexColorOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FGeometryScriptColorFlags ColorFlags;
	bool bClearExisting = false;
	bool bCreateColorSeam = false;
	bool bUseDataAttributeColor = false;
	FLinearColor Color = FLinearColor::White;
	FName ColorAttributeName;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
