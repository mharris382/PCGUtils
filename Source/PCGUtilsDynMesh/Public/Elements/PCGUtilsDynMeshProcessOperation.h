// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Selections/GeometrySelection.h"

#include "PCGUtilsDynMeshProcessOperation.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;
struct FPCGContext;

/** Domain in which an optional Selector is evaluated when the process does not require one itself. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProcessSelectionEvaluationDomain : uint8
{
	Triangle,
	Vertex,
	Edge
};

namespace PCGUtilsDynMeshProcess
{
	/** Maps the user-facing Selector evaluation domain onto the Geometry element type it means. */
	PCGUTILSDYNMESH_API UE::Geometry::EGeometryElementType ToGeometryElementType(
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain Domain);
}

/**
 * Immutable snapshot of the generic selection behaviour a process needs at evaluation time.
 *
 * Deferred evaluation happens from a *materializer's* context, where the original process node's settings are
 * unreachable. Everything the shared resolver needs is therefore captured here while the process node itself
 * is executing, rather than rediscovered later.
 */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessSelectionPolicy
{
	/** The operation cannot run without a selection (materialized Selection data or a connected Selector). */
	bool bRequiresSelection = false;

	/** The operation only accepts selections in one element domain; incoming selections are converted to it. */
	bool bRequiresSpecificDomain = false;
	UE::Geometry::EGeometryElementType RequiredDomain = UE::Geometry::EGeometryElementType::Face;

	/** Inclusive (any incident element) vs restrictive (all incident elements) domain conversion. */
	bool bAllowPartialDomainInclusion = true;

	/** Domain a connected Selector is evaluated in when the operation requires no specific domain. */
	EPCGUtilsDynMeshProcessSelectionEvaluationDomain SelectorEvaluationDomain =
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle;
};

/**
 * What a process does to the active selection travelling through a Builder chain. This is deliberately
 * independent of whether an immediate PCG node visually emits `UPCGDynamicMeshSelectionData` on its output
 * pin (`bOutputSelectionData`) - Builder-internal selection state and PCG pin presentation are different
 * concerns, and a Builder chain needs an active selection whether or not any node exposes one.
 */
enum class EPCGUtilsDynMeshProcessSelectionOutcome : uint8
{
	/** Topology preserving: the effective selection that entered the operation is still valid afterwards. */
	Preserve,

	/** The operation invalidated the incoming selection (elements deleted/renumbered) and produced none. */
	Clear,

	/** The operation produced its own selection; see FPCGUtilsDynMeshProcessOutcome::NewSelectionData. */
	Replace
};

/** Everything a process operation is handed when it runs, in either execution mode. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessInvocation
{
	/**
	 * The evaluation context: object allocation, logging, and engine calls needing an execution context.
	 *
	 * ARCHITECTURE WARNING: in deferred mode this belongs to the *materializer*, not to the node that authored
	 * the operation. Never call `Context->GetInputSettings<...>()` from inside an operation - all settings an
	 * operation needs must be captured into it while its own PCG node was executing.
	 */
	FPCGContext* Context = nullptr;

	/** The mesh to operate on. Owned by the caller and safe to mutate in place. */
	UPCGDynamicMeshData* MeshData = nullptr;

	/**
	 * The effective selection (incoming selection intersected with any Selector, converted to the operation's
	 * required domain), or null for a whole-mesh application.
	 */
	const UPCGDynamicMeshSelectionData* SelectionData = nullptr;

	/**
	 * The data MeshData was derived from, for operations that need to read attributes the working copy does
	 * not carry (a Data-domain attribute, say).
	 *
	 * CAUTION: this is only distinct from MeshData in immediate mode. In deferred mode the Builder subtree's
	 * result *is* the source, so the two alias. An operation must therefore never rely on reading unmodified
	 * geometry through this pointer while writing to MeshData - snapshot what it needs first.
	 */
	const UPCGDynamicMeshData* SourceMeshData = nullptr;

	/** Ordinal and count of concrete inputs being processed by the immediate node invocation. */
	int32 InputIndex = 0;
	int32 InputCount = 1;

	/**
	 * The Builder frame of the subtree being decorated - where its content was placed. Only set in deferred
	 * mode; concrete PCG data carries no such record, so an operation that offers a builder-local mode needs
	 * a documented fallback for immediate execution.
	 */
	bool bHasBuilderFrame = false;
	FTransform BuilderFrame = FTransform::Identity;
};

/** How the operation left the active selection. Defaults to the topology-preserving answer. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessOutcome
{
	EPCGUtilsDynMeshProcessSelectionOutcome SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	/** Only read when SelectionOutcome is Replace. Must reference the invocation's MeshData. */
	const UPCGDynamicMeshSelectionData* NewSelectionData = nullptr;

	/**
	 * A transform the operation applied to the geometry *as a whole*, reported so the Builder frame keeps
	 * following the content it describes.
	 *
	 * Leave at identity for anything that deforms in place (smoothing, warping, remeshing) rather than
	 * relocating the shape, and for anything that only moved part of the mesh - a partial transform says
	 * nothing about where the subtree as a whole now sits.
	 */
	FTransform GeometryTransform = FTransform::Identity;
};

/**
 * A reusable geometry algorithm, separated from PCG element execution.
 *
 * One instance is created by the process node while *that node* is executing, capturing every setting the
 * algorithm needs. The same instance then services both execution modes:
 *
 *   Immediate: the process element runs it against the realized input mesh.
 *   Deferred:  a Builder decorator stores it and runs it when the chain is materialized for a seed.
 *
 * Implementations must be immutable and free of per-evaluation state: one operation is evaluated for every
 * seed a materializer realizes, and the same Builder expression may be consumed by several materializers.
 * Hold value types and soft references only - a live `FPCGContext*` or a hard `UObject*` must never be
 * captured here (the context would be stale, and the object would be invisible to GC).
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessOperation
	: public TSharedFromThis<FPCGUtilsDynMeshProcessOperation>
{
public:
	virtual ~FPCGUtilsDynMeshProcessOperation() = default;

	/** Runs the algorithm. Returns false (already logged) to discard this input/seed. */
	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const = 0;
};
