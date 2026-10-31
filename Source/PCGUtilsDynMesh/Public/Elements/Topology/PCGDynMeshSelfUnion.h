// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshSelfUnion.generated.h"

/**
 * Unions a mesh with itself: parts that pass through each other become one solid, and the faces left inside it
 * are removed. Geometry Script's Apply Mesh Self Union, with its options.
 *
 * Use it on a mesh made by merging pieces that overlap, before anything that needs a clean solid, such as a
 * Boolean or a cut.
 *
 * Whole mesh only. A self union decides what is inside the solid from every part of the mesh at once, so there is
 * no meaning to running it on some of the triangles. A selection, whether it arrives as selection data or from a
 * connected Selector, is refused with a warning and that input is skipped.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology", HideCategories=(Selector),
	meta=(Keywords="DynMesh mesh self union boolean resolve intersections overlap solid merge csg"))
class PCGUTILSDYNMESH_API UPCGDynMeshSelfUnionSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** Mesh properties written to @Data after this node runs, as DynMesh | Properties writes them. All off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Properties", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties OutputProperties;

	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &OutputProperties; }

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelfUnion"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	/** Fill Holes, Trim Flaps, Simplify Output, Simplify Planar Tolerance and Winding Threshold, as Geometry Script defines them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Union", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptMeshSelfUnionOptions Options;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelfUnionOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FGeometryScriptMeshSelfUnionOptions Options;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshSelfUnionElement final : public FPCGUtilsDynMeshProcessBaseElement
{
};
