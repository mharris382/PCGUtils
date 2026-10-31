// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Target/PCGUtilsPainterTarget.h"

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Copies a painted mesh's vertex colors onto another level of detail of the same mesh, so every LOD shows the
 * same paint instead of each LOD being painted on its own. Both meshes must be in the same local space, as the
 * LODs of one asset are.
 *
 * Each target vertex takes the color of the closest point on the painted surface. With Keep Islands Together,
 * each island of the target first picks the one painted island most of its vertices land on, and then projects
 * only onto that island. A lower-LOD leaf therefore never mixes the colors of two neighbouring leaves, which is
 * what keeps Random Value by Mesh Island consistent across LODs.
 */
namespace PCGUtilsPainterLODTransfer
{
	struct FTransferOptions
	{
		/** Channels copied from the painted mesh. The others keep the base color. */
		EPCGUtilsDynMeshPainterColorChannel Channels = EPCGUtilsDynMeshPainterColorChannel::All;

		/** Where the target's channels that are not copied (and vertices that fail to project) start from. */
		EPCGUtilsPainterBaseColorSource BaseColorSource = EPCGUtilsPainterBaseColorSource::CanonicalExisting;
		FVector4f ConstantBaseColor = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);

		/** Restrict each target island to the one painted island most of its vertices project onto. */
		bool bKeepIslandsTogether = true;

		/** When non-null, only these target vertex IDs are written. */
		const TSet<int32>* SelectedVertexIDs = nullptr;
	};

	struct FTransferResult
	{
		/** Target vertices written with a color from the painted mesh. */
		int32 NumTransferred = 0;

		/** Target vertices that found no painted surface and were written with the base color. */
		int32 NumFailed = 0;
	};

	/**
	 * Transfers Painted's primary vertex colors onto Target's primary color overlay (created when missing).
	 * Returns false when Painted has no color overlay or no triangles; Target is then unchanged.
	 */
	PCGUTILSPAINTER_API bool TransferPaint(
		const UE::Geometry::FDynamicMesh3& Painted,
		UE::Geometry::FDynamicMesh3& Target,
		const FTransferOptions& Options,
		FTransferResult& OutResult);

	/**
	 * The island of every vertex (triangles sharing a vertex are one island), indexed by vertex ID; INDEX_NONE for
	 * unused IDs. Islands are numbered by their root vertex, not densely.
	 */
	PCGUTILSPAINTER_API void FindVertexIslands(const UE::Geometry::FDynamicMesh3& Mesh, TArray<int32>& OutIslandOfVertex);
}
