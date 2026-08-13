#pragma once

#include "CoreMinimal.h"

class UPCGDynamicMeshData;
struct FPCGContext;

/**
 * Small shared helper for PCGUtilsDynMesh nodes that need to relate PCG-authored world-space geometry (points,
 * directions) to a Dynamic Mesh's own coordinate space. Mirrors PCGUtilsSplineHelpers::ResolveActorTransformForSpline
 * - not a general transform framework.
 */
namespace PCGUtilsDynMeshSpaceHelpers
{
	/**
	 * Resolves the PCG target actor's world transform, for converting between world space and the coordinate
	 * space Dynamic Mesh data is expected to be in - matching the established PCGDynMeshActorSpaceTransform /
	 * PCGUtilsSplineHelpers convention (Dynamic Mesh data defaults to target-actor-local space).
	 *
	 * If bConvertToLocalSpace is false, returns FTransform::Identity (callers should then treat world-space
	 * geometry as already being in the mesh's coordinate space). If true but no target actor can be resolved,
	 * logs a warning and also returns Identity, so callers degrade gracefully rather than producing garbage output.
	 *
	 * Callers apply the returned transform themselves, in whichever direction their computation needs:
	 *   - mesh-local position -> world:        ActorTransform.TransformPosition(LocalPos)
	 *   - world position -> mesh-local:         ActorTransform.InverseTransformPosition(WorldPos)
	 *   - world direction -> mesh-local:        ActorTransform.InverseTransformVectorNoScale(WorldDir)
	 */
	PCGUTILSDYNMESH_API FTransform ResolveMeshActorTransform(
		FPCGContext* Context, const UPCGDynamicMeshData* MeshData, bool bConvertToLocalSpace);
}
