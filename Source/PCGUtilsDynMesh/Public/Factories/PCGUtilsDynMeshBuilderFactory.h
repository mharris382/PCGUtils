// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshOperation.h"

#include "PCGUtilsDynMeshBuilderFactory.generated.h"

class UDynamicMesh;
class UPCGBasePointData;
class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;

namespace PCGUtilsDynMeshBuilderFactoryConstants
{
	/** User-facing pin label for a single Builder expression. `Factory` never appears on a graph surface. */
	inline const FName OutputPin = TEXT("Builder");

	/** Multi-input pin label for future combining/binary Builder nodes. */
	inline const FName BuildersInputPin = TEXT("Builders");
}

USTRUCT(meta=(PCG_DataTypeDisplayName="DynMesh Builder"))
struct FPCGUtilsDynMeshBuilderFactoryDataTypeInfo : public FPCGUtilsDynMeshFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

/**
 * Everything a Builder expression is given when a materializer realizes it for one seed.
 *
 * ARCHITECTURE WARNING: `Context` is the *materializer's* execution context (Create Primitive, a future
 * Build Dynamic Mesh node, ...). It exists for object allocation, logging, and engine calls that require an
 * execution context. A deferred operation must NEVER call `Context->GetInputSettings<...>()` through it - the
 * settings reachable that way belong to the materializer's node, not to the node that authored the operation.
 * Everything a deferred operation needs must be captured while its own PCG node was executing.
 */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshBuildContext
{
	FPCGContext* Context = nullptr;

	/** The seed's transform, already converted into the space the Builder chain is expected to produce. */
	FTransform SeedTransform = FTransform::Identity;

	/** The seed's own local-space (unscaled) bounds. */
	FBox SeedLocalBounds = FBox(ForceInit);

	/** Source point data and index for the seed being realized, for future attribute-driven Builders. */
	const UPCGBasePointData* SeedData = nullptr;
	int32 SeedIndex = INDEX_NONE;
};

/**
 * The realized value of a Builder expression: a PCG DynMesh result that may carry an active selection.
 *
 * `MeshData` is the authoritative realized mesh container, so materials and every other PCG DynMesh concern
 * stay attached correctly - there is deliberately no second, parallel material-array system here.
 *
 * The mesh is *private to the subtree currently being evaluated*: a leaf allocates it and each decorator above
 * is free to mutate it in place. Do not clone geometry between decorators.
 */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshBuildResult
{
	UPCGDynamicMeshData* MeshData = nullptr;

	/** Optional active selection. When present it always references `MeshData`. */
	const UPCGDynamicMeshSelectionData* SelectionData = nullptr;

	/**
	 * Where this subtree's content sits: the rigid frame a leaf placed its primitive at, after fitting and
	 * alignment resolved against the seed. Expressed in the same space as the geometry (target-actor-local).
	 *
	 * This is what makes "the builder's own space" a thing a downstream decorator can transform in. Without
	 * it, scaling a Builder scales its vertices away from the actor origin instead of making the shape bigger
	 * where it stands, and building a column out of two boxes needs four primitives instead of two.
	 *
	 * Deliberately rigid: a pivot has a position and an orientation, not a size. Scaling a Builder about its
	 * own frame therefore leaves the frame exactly where it was, which is what makes repeated builder-local
	 * transforms compose predictably.
	 */
	bool bHasBuilderFrame = false;
	FTransform BuilderFrame = FTransform::Identity;

	bool IsValid() const;
	bool HasSelection() const { return SelectionData != nullptr; }

	/**
	 * The result expressed as the PCG data a process would have received on its main input:
	 * the selection when one is active, otherwise the bare mesh. Feed this to
	 * `FPCGUtilsDynMeshProcessFunctions::ResolveInput()`.
	 */
	const UPCGData* GetProcessInputData() const;

	/** Replaces the realized mesh, dropping any selection that referenced the previous one. */
	void SetMeshData(UPCGDynamicMeshData* InMeshData);

	/** Drops the active selection without touching the mesh. */
	void ClearSelection() { SelectionData = nullptr; }

	/** Sets the active selection. It must already reference `MeshData`. */
	void SetSelectionData(const UPCGDynamicMeshSelectionData* InSelectionData);

	/** Records where this subtree's content was placed. Only the rotation and location are kept. */
	void SetBuilderFrame(const FTransform& InFrame);

	/**
	 * Carries the recorded frame along with a transform already applied to the whole geometry, so the frame
	 * keeps tracking the content it describes. A no-op when no frame has been recorded.
	 */
	void MoveBuilderFrame(const FTransform& GeometryTransform);
};

class FPCGUtilsDynMeshBuilderOperation;

/**
 * Immutable Builder configuration transported through PCG pins - one node of the deferred geometry expression
 * the PCG graph describes. Leaves create geometry, unary decorators process it, binary operations combine two
 * expressions, and a materializer evaluates the whole thing against seed context.
 *
 * Factory instances are immutable once their provider emits them; mutable, per-evaluation state belongs on the
 * non-UObject operation created by `CreateOperation()`.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Builders")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshBuilderFactoryData : public UPCGUtilsDynMeshFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo)

	/**
	 * Creates, context-binds, and prepares a runtime operation. Common initialization cannot be bypassed by
	 * subclasses. Returns null when preparation fails (already logged).
	 */
	TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperationInternal() const;
};

/**
 * Runtime, per-seed evaluation of one node of a Builder expression. Created once by the materializer and then
 * evaluated once per seed, so per-node caches (a reference mesh, a resolved actor transform, ...) belong here
 * rather than being recomputed per seed.
 *
 * Ownership of the returned result passes to the caller: it is private to the subtree being evaluated, so a
 * decorator above may mutate it in place.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshBuilderOperation : public FPCGUtilsDynMeshOperation
{
public:
	/**
	 * One-time setup after the operation is bound to the materializer's context - this is where a decorator
	 * creates its child operations. Returning false aborts operation creation.
	 */
	virtual bool Prepare() { return true; }

	/** Realizes this expression for one seed. Returns false (already logged) when the seed produced nothing. */
	virtual bool Build(
		const FPCGUtilsDynMeshBuildContext& BuildContext, FPCGUtilsDynMeshBuildResult& OutResult) const = 0;
};

namespace PCGUtilsDynMeshFactories
{
	/** The set of PCG data types accepted anywhere a DynMesh Builder expression is expected. */
	PCGUTILSDYNMESH_API const TSet<FPCGDataTypeBaseId>& GetBuilderFactoryTypes();
}
