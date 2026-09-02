// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

/**
 * Reusable, geometry-only backend for writing per-component Static Mesh override vertex colors
 * (`UStaticMeshComponent::LODData[LOD].OverrideVertexColors`).
 *
 * It knows nothing about PCG, the Painter framework, LOD policy, or component targeting. Callers own the
 * transaction / package-dirtying policy and decide which LODs to write. The write path mirrors the engine's
 * runtime `FMeshVertexPainter` (`Engine/Private/MeshVertexPainter/MeshVertexPainter.cpp`).
 *
 * Game thread only. Editor-authoring oriented: reading render-vertex positions needs a CPU copy of the
 * position buffer, which the editor always keeps but a cooked build only keeps when the mesh sets
 * `bAllowCPUAccess`.
 */
namespace PCGUtilsPainterStaticMeshBackend
{
	/** How the starting color of each render vertex is seeded before a Painter modifies it. */
	enum class EBaseColorMode : uint8
	{
		/** Component override colors if present and correctly sized, else asset vertex colors, else opaque white. */
		Existing,
		/** The Static Mesh asset's own vertex color buffer for that LOD, else opaque white. */
		AssetVertexColors,
		White,
		Black,
	};

	/** Number of mesh LODs, or 0 if the component has no static mesh / render data. */
	PCGUTILSPAINTER_API int32 GetNumLODs(const UStaticMeshComponent* Component);

	/** Render-vertex count of a LOD, or 0 if the LOD is unavailable. */
	PCGUTILSPAINTER_API int32 GetLODRenderVertexCount(const UStaticMeshComponent* Component, int32 LODIndex);

	/**
	 * Fills OutPositions / OutNormals (mesh-local, one entry per render vertex) for the LOD.
	 * Returns false if the LOD or its CPU-side position data is unavailable.
	 */
	PCGUTILSPAINTER_API bool GetLODRenderVertices(
		const UStaticMeshComponent* Component,
		int32 LODIndex,
		TArray<FVector3f>& OutPositions,
		TArray<FVector3f>& OutNormals);

	/**
	 * Resolves the starting color array for a LOD according to Mode. Always resizes OutColors to the LOD's
	 * render-vertex count; unresolved entries fall back to White (or Black for EBaseColorMode::Black).
	 */
	PCGUTILSPAINTER_API void GetBaseLODColors(
		const UStaticMeshComponent* Component,
		int32 LODIndex,
		EBaseColorMode Mode,
		TArray<FColor>& OutColors);

	/**
	 * Writes a complete per-render-vertex color array to one LOD's OverrideVertexColors and initializes its RHI
	 * resource. `Colors.Num()` must equal `GetLODRenderVertexCount(Component, LODIndex)`. Grows `LODData` as
	 * needed. Does not recreate render state — call FinalizeVertexColorEdit once after a batch of LODs.
	 */
	PCGUTILSPAINTER_API bool SetOverrideVertexColorsForLOD(
		UStaticMeshComponent* Component,
		int32 LODIndex,
		TConstArrayView<FColor> Colors);

	/**
	 * Recreates the component's render state so the scene proxy binds the new override buffers. In the editor
	 * also caches painted-vertex data and pairs the component's Static Mesh DDC key, so the result persists and
	 * behaves like an interactively mesh-painted component (and is not spuriously remapped on the next load).
	 */
	PCGUTILSPAINTER_API void FinalizeVertexColorEdit(UStaticMeshComponent* Component);
}
