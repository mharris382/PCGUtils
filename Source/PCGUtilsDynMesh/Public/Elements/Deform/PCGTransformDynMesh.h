// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGTransformDynMesh.generated.h"

/** The frame a Transform DynMesh's transform is expressed in. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshTransformSpace : uint8
{
	/**
	 * The mesh's own coordinates, which for DynMesh data means target-actor-local space (the module default).
	 * A scale here pushes vertices away from the actor origin.
	 */
	ActorLocal,

	/** World coordinates. The mesh is converted to world, transformed, and converted back. */
	World,

	/**
	 * The mesh's own bounding-box centre. A scale here grows the mesh where it stands instead of pushing it
	 * away from the actor origin.
	 */
	DynMeshLocal,

	/**
	 * The frame the Builder recorded when it placed its content - a primitive's own pivot after fitting and
	 * alignment. This is the one that lets a column be two Box Builders rather than four: transform and scale
	 * each one about its own placement to get a wider slab at the top and bottom.
	 *
	 * Only concrete in deferred (Builder) mode. Immediate DynMesh data carries no such record, so this falls
	 * back to DynMesh Local - the mesh's own bounds centre - which is the closest honest equivalent.
	 */
	BuilderLocal
};

/**
 * The reusable Transform algorithm, independent of PCG element execution.
 *
 * This one object services both execution modes: the immediate executor runs it against realized DynMesh data,
 * and a deferred Builder decorator stores it and runs it per seed when the chain is materialized. It captures
 * every setting it needs at construction and never reaches back into a PCG context for them.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshTransformOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FTransform Transform = FTransform::Identity;

	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::ActorLocal;

	/** Whole-mesh application only; Geometry Script's selection overload has no equivalent option. */
	bool bFixOrientationForNegativeScale = true;

	/** Recompute normals on triangles touching a moved vertex. Whole-mesh transforms handle normals natively. */
	bool bRecomputeSelectionNormals = true;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;

private:
	/**
	 * The transform that maps the chosen space's coordinates into the mesh's own coordinates. Every space is
	 * then handled by one conjugation, `Frame.Inverse() * Transform * Frame`.
	 */
	FTransform ResolveSpaceFrame(const FPCGUtilsDynMeshProcessInvocation& Invocation) const;
};

/**
 * Applies a transform to a DynMesh, or to just the vertices covered by a DynMesh Selection / Selector.
 *
 * Topology preserving, so an active Builder selection stays valid across this node and is deliberately
 * preserved rather than cleared. Connecting a DynMesh Builder to the input makes this node defer: it emits a
 * Builder describing the transform instead of touching any geometry.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Deform")
class PCGUTILSDYNMESH_API UPCGTransformDynMeshSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("TransformDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Offset, rotation, and scale applied to the mesh, expressed in Space below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	FTransform Transform;

	/**
	 * Which frame the transform is expressed in. Builder Local is the interesting one: it transforms the shape
	 * about the pivot its Builder placed it at, so scaling makes the shape bigger where it stands rather than
	 * pushing it away from the actor origin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::ActorLocal;

	/** Flips triangle orientation when the transform's scale is negative, so the mesh does not end up inside out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", AdvancedDisplay, meta=(PCG_Overridable))
	bool bFixOrientationForNegativeScale = true;

	/**
	 * When only part of the mesh is transformed, recompute normals for the triangles touching a moved vertex,
	 * leaving the rest of the mesh's shading untouched. Has no effect on a whole-mesh transform, which already
	 * carries normals through natively.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", AdvancedDisplay, meta=(PCG_Overridable))
	bool bRecomputeSelectionNormals = true;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Fully implemented through CreateProcessOperation, so it is safe to expose the Builder contract. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor verbatim: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGTransformDynMeshElement : public FPCGUtilsDynMeshProcessBaseElement
{
};
