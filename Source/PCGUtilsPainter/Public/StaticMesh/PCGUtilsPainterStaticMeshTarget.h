// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "StaticMesh/PCGUtilsPainterStaticMeshBackend.h"
#include "Target/PCGUtilsPainterTarget.h"

class UStaticMeshComponent;

/**
 * Painter target for a `UStaticMeshComponent`'s per-component override vertex colors.
 *
 * `Prepare()` builds a transient `FDynamicMesh3` of LOD0 by position-welding the render buffers: render
 * vertices within `WeldTolerance` become one canonical base vertex, and per-render-vertex color / normal / UV
 * differences stay in overlays. This reconnects UV / hard-normal / material / render-vertex seam duplicates
 * (which the build emits at identical positions) but, having no access to authored vertex identity, it can
 * also merge two authored-distinct vertices that happen to sit within tolerance — see PCGUtilsPainter.md,
 * "Canonical topology". Non-manifold triangles are retained by splitting their corners; degenerate/duplicate
 * triangles are dropped. A primary normal overlay is seeded from LOD0's render tangent-Z so Painter samples
 * get real (baked, seam-preserving) surface normals. `Prepare()` records the exact render-vertex ->
 * canonical-vertex and render-vertex -> color-overlay-element correspondence and builds the LOD0 AABB tree once.
 *
 * `Commit()` writes LOD0's override colors straight from that exact correspondence, then — unless
 * `bTransferToLowerLODs` is false — projects every lower LOD's render vertices onto the painted LOD0 surface
 * and transfers only `WrittenChannels`, preserving each lower LOD's other channels. All quantization / sRGB
 * happens at the single final `FColor` write boundary.
 *
 * Editor-authoring, game thread. The caller owns `Component->Modify()`, the transaction, and render-state
 * finalization ordering across a batch of components.
 */
struct PCGUTILSPAINTER_API FPCGUtilsPainterStaticMeshTarget final : public FPCGUtilsPainterTarget
{
	struct FConfig
	{
		/** Seeds the canonical LOD0 color overlay and every lower LOD's preserved base color. */
		PCGUtilsPainterStaticMeshBackend::EBaseColorMode BaseColorMode =
			PCGUtilsPainterStaticMeshBackend::EBaseColorMode::White;

		/** Channels the Painter is allowed to change; drives lower-LOD channel preservation on Commit. */
		EPCGUtilsDynMeshPainterColorChannel WrittenChannels = EPCGUtilsDynMeshPainterColorChannel::All;

		/** Encode the painted value as sRGB when packing the 8-bit color (matches the node's Convert To sRGB). */
		bool bConvertToSRGB = false;

		/** Evaluate LOD0 only, or LOD0 + closest-point transfer to every lower LOD. */
		bool bTransferToLowerLODs = true;

		/** Render vertices within this distance are treated as one canonical vertex (asset local units). */
		double WeldTolerance = 0.01;
	};

	FPCGUtilsPainterStaticMeshTarget(UStaticMeshComponent* InComponent, const FConfig& InConfig);
	virtual ~FPCGUtilsPainterStaticMeshTarget() override;

	/** Builds the canonical LOD0 mesh, correspondence, and AABB tree. Logs a graph warning and returns false on failure. */
	bool Prepare(FPCGContext* Context);

	/** True after a successful Prepare(). */
	virtual bool IsValid() const override { return bPrepared; }
	virtual UE::Geometry::FDynamicMesh3* GetCanonicalMesh() override { return bPrepared ? &CanonicalMesh : nullptr; }
	virtual const UPCGDynamicMeshData* GetCanonicalMeshData() const override { return CanonicalProxyData; }
	virtual bool IsNativeDynMeshTarget() const override { return false; }
	virtual FTransform GetLocalToWorld() const override { return ComponentToWorld; }
	virtual bool Commit(FPCGContext* Context) override;

	/** Number of LODs whose override colors Commit() actually wrote. */
	int32 GetPaintedLODCount() const { return PaintedLODCount; }

private:
	FConfig Config;
	UStaticMeshComponent* Component = nullptr;
	FTransform ComponentToWorld = FTransform::Identity;

	bool bPrepared = false;
	int32 PaintedLODCount = 0;

	UE::Geometry::FDynamicMesh3 CanonicalMesh;
	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> CanonicalTree;

	/**
	 * A `UPCGDynamicMeshData` copy of `CanonicalMesh` (identical vertex/triangle IDs), created on the PCG
	 * context so it lives for the element execution. Lets Painter operations reuse DynMesh Geometry Script
	 * utilities (selector domain conversion) on a Static Mesh target. Raw pointer: GC-rooted by the context.
	 */
	class UPCGDynamicMeshData* CanonicalProxyData = nullptr;

	/** Size = LOD0 render-vertex count. Maps each render vertex to its canonical base-vertex ID. */
	TArray<int32> RenderVertexToCanonicalVID;

	/**
	 * Size = LOD0 render-vertex count. The primary color-overlay element to read for each render vertex when
	 * committing LOD0. Identity in the common case (element i == render vertex i); a render vertex that ended up
	 * only on a duplicated non-manifold triangle points at that triangle's replacement element instead.
	 */
	TArray<int32> RenderVertexToReadElement;

	/**
	 * Size = LOD0 render-vertex count. The seeded LOD0 base color per render vertex, as a plain normalized
	 * value (byte/255, no sRGB decode) — the fallback for a render vertex not covered by any canonical write.
	 */
	TArray<FVector4f> LOD0SeedColors;

	void CommitLOD0(TArray<FColor>& OutColors) const;
	bool TransferToLOD(int32 LODIndex, FPCGContext* Context);
};
