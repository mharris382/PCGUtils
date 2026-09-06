// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGDynMeshSetPolygroup.generated.h"

/**
 * Assigns one PolyGroup ID to a triangle selection of a Dynamic Mesh (GeometryScript
 * `SetPolygroupForMeshSelection`). The Mesh input accepts a whole Dynamic Mesh, a DynMesh Selection, or a
 * bare mesh plus a Selector; when no selection is supplied the whole mesh is treated as the selection. Works
 * in the Triangle (face) selection domain, declared through `GetRequiredSelectionDomain()`.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh selection selector polygroup poly group set assign id"))
class PCGUTILSDYNMESH_API UPCGDynMeshSetPolygroupSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSetPolygroup"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Which PolyGroup layer to write. The default layer always exists; an extended layer at the given index is
	 * created if the mesh does not already have it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroup", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptGroupLayer GroupLayer;

	/** PolyGroup ID assigned to every selected triangle. Ignored when Generate New PolyGroup is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroup",
		meta=(PCG_Overridable, EditCondition="!bGenerateNewPolygroup"))
	int32 PolygroupID = 0;

	/** Allocate a fresh unused PolyGroup ID for the selection instead of using PolyGroup ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroup", meta=(PCG_Overridable))
	bool bGenerateNewPolygroup = false;

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
class PCGUTILSDYNMESH_API FPCGDynMeshSetPolygroupElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSetPolygroupOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FGeometryScriptGroupLayer GroupLayer;
	int32 PolygroupID = 0;
	bool bGenerateNewPolygroup = false;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
