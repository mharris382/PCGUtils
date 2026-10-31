#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGBevelEdges.generated.h"

/**
 * Bevels all edges or the effective edge selection using Geometry Script's underlying bevel operation.
 * Newly created bevel faces form the result selection; the topology base handles result groups and Selectors.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector bevel edge chamfer"))
class PCGUTILSDYNMESH_API UPCGBevelEdgesSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGBevelEdgesSettings();
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("BevelEdges"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	/**
	 * Distance each beveled edge is inset from its initial position. Must not be negative.
	 *
	 * @Data mode reads one value per input from the input's @Data domain: a number (Integer32, Integer64, Float or
	 * Double) of 0 or more. The @Data. prefix is optional on DynMesh data. The attribute is required on every
	 * input: an input without it, with a non-numeric value, or with a negative value fails with an error and is
	 * skipped; the constant is never used as a fallback. Builder inputs are refused in @Data mode, because a
	 * Builder has no @Data until it is realized.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bevel", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs BevelDistance;

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

	/**
	 * Beveling is defined on edges. Declaring that here lets the shared resolver convert any incoming
	 * selection domain (and any Selector's evaluation domain) to edges, instead of erroring on a triangle
	 * selection - which is what the module's domain-agnostic pin contract requires, and what makes a
	 * Selector usable at all in deferred mode.
	 */
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Edge;
		return true;
	}

	/** A Builder carries no @Data until realized, so @Data-driven Bevel Distance only runs on realized meshes. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return BevelDistance.IsConstant(); }
	virtual FText GetBuilderInputRejectionReason() const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* InContext) const override;
	virtual FName GetMainInputPinLabel() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGBevelEdgesElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

/**
 * ApplyMeshBevelEdgeSelection scopes its own effect, so no Mesh Target region extraction or weld is needed -
 * the operation bevels the mesh it was handed, which is already private to the caller.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshBevelEdgesOperation final : public FPCGUtilsDynMeshTopologyOperation
{
public:
	/** Per-input source of the distance; in @Data mode Execute resolves it into BevelDistance on a local copy. */
	FPCGUtilsInputShorthandDoubleAbs BevelDistanceInput;
	float BevelDistance = 1.0f;
	int32 Subdivisions = 0;
	float RoundWeight = 1.0f;
	bool bInferMaterialID = false;
	int32 SetMaterialID = 0;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;

protected:
	virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FGeometrySelection* Selection, TArray<int32>& OutResultTriangles) const override;
};
