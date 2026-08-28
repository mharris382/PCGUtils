#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Materials/MaterialInterface.h"

#include "PCGMaterial.generated.h"

/** Assigns a material to an entire Dynamic Mesh or appends it for a mesh selection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGMaterialSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("Material"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Material assigned to the whole mesh or to the supplied selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Slot-zero material used when a selection is applied to a mesh with no established material assignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialInterface> DefaultMaterial;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Topology preserving and fully implemented through its operation, so a Builder can be decorated. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGMaterialElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

/**
 * Assigns one material to a whole mesh, or appends a material and assigns it to just the effective selection.
 * Material assignment never changes topology, so an active Builder selection survives it.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshMaterialOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	/** Resolved at capture time, not evaluation time: an operation must not touch the asset registry later. */
	TObjectPtr<UMaterialInterface> AssignedMaterial;
	TObjectPtr<UMaterialInterface> DefaultMaterial;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
