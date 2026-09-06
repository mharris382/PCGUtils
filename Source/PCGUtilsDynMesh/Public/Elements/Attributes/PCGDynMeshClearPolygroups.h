// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGDynMeshClearPolygroups.generated.h"

/**
 * Resets the PolyGroup assignments of a Dynamic Mesh triangle selection to a clear value (GeometryScript
 * `ClearPolygroups`, which has no selection option of its own). The selected triangles are extracted into a
 * region via the PCGUtilsDynMesh Mesh Target handle, cleared, and welded back, so unselected triangles keep
 * their groups. With no selection the whole layer is cleared. Works in the Triangle (face) selection domain.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh selection selector polygroup poly group clear reset remove"))
class PCGUTILSDYNMESH_API UPCGDynMeshClearPolygroupsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshClearPolygroups"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Which PolyGroup layer to clear. Clearing has no effect if the layer does not exist on the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroup", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptGroupLayer GroupLayer;

	/** PolyGroup ID written to every cleared triangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroup", meta=(PCG_Overridable))
	int32 ClearValue = 0;

	virtual bool GetRequiredSelectionDomain(
		UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Only the PolyGroup attribute changes, so a Builder can be decorated and its active selection survives. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGDynMeshClearPolygroupsElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshClearPolygroupsOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FGeometryScriptGroupLayer GroupLayer;
	int32 ClearValue = 0;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
