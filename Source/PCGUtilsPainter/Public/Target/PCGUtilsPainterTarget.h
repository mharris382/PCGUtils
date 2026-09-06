// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

namespace UE::Geometry { class FDynamicMesh3; }
class UPCGDynamicMeshData;
class UPCGUtilsDynMeshPainterFactoryData;
struct FPCGContext;

/**
 * The Painter execution layer's canonical target abstraction.
 *
 * Every Painter graph evaluates against a canonical `FDynamicMesh3`, regardless of the original target domain.
 * A concrete target is responsible for:
 *
 *   - producing that canonical mesh (a native Dynamic Mesh exposes its own; a Static Mesh Component builds a
 *     transient connectivity-preserving representation of LOD0);
 *   - reporting the canonical-mesh-local -> world transform used to fill world-space Painter samples;
 *   - carrying enough correspondence to write the painted canonical colors back to the real target, including
 *     propagation to lower LODs where the target has them.
 *
 * Individual Painter factories never see this type. They keep evaluating through their normal operation
 * interface; `EvaluatePainterGraphOntoTarget` drives the shared traversal. This is the single place target
 * conversion, LOD propagation, and write-back live — a Painter factory must never contain Static Mesh,
 * Geometry Collection, or other target-specific logic.
 */
struct PCGUTILSPAINTER_API FPCGUtilsPainterTarget
{
	virtual ~FPCGUtilsPainterTarget() = default;

	/** True once the canonical mesh is available (after any required build step). */
	virtual bool IsValid() const = 0;

	/** The canonical geometry the Painter graph evaluates against and writes its primary color overlay to. */
	virtual UE::Geometry::FDynamicMesh3* GetCanonicalMesh() = 0;

	/**
	 * A `UPCGDynamicMeshData` view of the canonical mesh — the real graph data for a native Dynamic Mesh
	 * target, a transient wrapper the target materializes for other targets. Non-null once `IsValid()`.
	 * Painter operations reuse it for Geometry Script utilities (e.g. DynMesh selection domain conversion).
	 */
	virtual const UPCGDynamicMeshData* GetCanonicalMeshData() const { return nullptr; }

	/**
	 * True only when this target originated as a graph `UPCGDynamicMeshData` that participates in
	 * DynMesh<->Points dataset pairing. `Painter by Vertex ID` requires it; every other Painter ignores it.
	 */
	virtual bool IsNativeDynMeshTarget() const { return false; }

	/** Canonical-mesh-local -> world. Identity when the caller wants purely local evaluation. */
	virtual FTransform GetLocalToWorld() const { return FTransform::Identity; }

	/** Pairing coordinates for Painters backed by ordered per-DynMesh external datasets. */
	virtual int32 GetDataSetIndex() const { return 0; }
	virtual int32 GetDataSetCount() const { return 1; }

	/**
	 * Commits the painted canonical primary color overlay back to the real target. For a native Dynamic Mesh
	 * this is a no-op (the overlay was written in place). For a Static Mesh Component it writes LOD0's override
	 * colors from the exact canonical correspondence and, unless disabled, transfers only `WrittenChannels`
	 * to every lower LOD by closest-point surface projection.
	 *
	 * @return false on a hard failure that produced no write (already logged); true otherwise, including
	 *         partial per-LOD skips.
	 */
	virtual bool Commit(FPCGContext* Context) = 0;
};

/** Where the canonical base color of each vertex comes from before the Painter modifies its write channels. */
enum class EPCGUtilsPainterBaseColorSource : uint8
{
	/** Read the existing canonical primary color overlay (which a target may have pre-seeded). */
	CanonicalExisting,
	/** Ignore existing color; start every vertex from `ConstantBaseColor`. */
	Constant
};

/** Inputs to the shared Painter traversal. Target-agnostic. */
struct PCGUTILSPAINTER_API FPCGUtilsPainterGraphEvaluation
{
	/** The single resolved Painter for this graph. Required. */
	const UPCGUtilsDynMeshPainterFactoryData* Painter = nullptr;

	/** Destination channels the Painter is permitted to change; other channels keep the base color. */
	EPCGUtilsDynMeshPainterColorChannel WriteChannels = EPCGUtilsDynMeshPainterColorChannel::All;

	EPCGUtilsPainterBaseColorSource BaseColorSource = EPCGUtilsPainterBaseColorSource::CanonicalExisting;

	/** Used when `BaseColorSource == Constant`, and as the fallback when Existing has no color. */
	FVector4f ConstantBaseColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

	/** When non-null, only these canonical vertex IDs are evaluated and written (selection-scoped). */
	const TSet<int32>* SelectedVertexIDs = nullptr;
};

namespace PCGUtilsPainter
{
	/**
	 * Evaluates a complete Painter graph once over the canonical mesh of `Target` and writes the resolved
	 * colors into its primary color overlay, seam-consistently (every color element attached to a geometric
	 * vertex receives that vertex's result). Does not Commit — the caller decides when to write back.
	 *
	 * @return false (already logged) when the Painter could not initialize against this target, e.g. a
	 *         Dynamic Mesh-only Painter targeting a non-DynMesh canonical representation.
	 */
	PCGUTILSPAINTER_API bool EvaluatePainterGraphOntoTarget(
		FPCGUtilsPainterTarget& Target,
		const FPCGUtilsPainterGraphEvaluation& Evaluation,
		FPCGContext* Context);
}
