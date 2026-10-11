#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Materials/MaterialInterface.h"

#include "PCGMaterial.generated.h"

/**
 * Assigns a material to an entire Dynamic Mesh, or to just a selection of it.
 *
 * With a selection, only the selected triangles change. They are moved to the slot that already holds the
 * material, or to a new slot added at the end if no slot holds it. Every other triangle keeps its slot and every
 * existing material stays in the list, so the node can be run repeatedly to build up surfaces by selection.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector material assign set"))
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

	/**
	 * Material for the triangles outside the selection when the mesh arrives with no material list of its own.
	 * They are given this rather than the selection's material. May be left empty, which leaves those slots
	 * without a material. Unused when the mesh already has materials, or when there is no selection.
	 */
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
 * Assigns one material to a whole mesh, or to just the effective selection, reusing the slot that already holds
 * the material and adding one only when none does.
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
