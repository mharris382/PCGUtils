#pragma once

#include "CoreMinimal.h"

namespace UE::Geometry { class FDynamicMesh3; }

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
	/** Exact source-local -> world -> destination-local conversion, including shear between rotated nonuniform frames.
	 * Does not collapse the two frames into FTransform::GetRelativeTransform or FTransform::Inverse.
	 * Transforms normals by inverse transpose, tangents as vectors, and corrects reflected triangle winding. */
	PCGUTILSDYNMESH_API void TransformMeshBetweenSpaces(
		UE::Geometry::FDynamicMesh3& Mesh, const FTransform& SourceToWorld, const FTransform& DestinationToWorld);

	/** Transform a surface normal correctly under nonuniform scale (unlike TransformVectorNoScale). */
	PCGUTILSDYNMESH_API FVector TransformNormalToWorld(const FTransform& MeshToWorld, const FVector& Normal);

	/** Apply position and normal/tangent conversion, retaining a unit-scale orthonormal point frame. */
	PCGUTILSDYNMESH_API FTransform TransformSurfaceFrameToWorld(
		const FTransform& MeshToWorld, const FVector& Position, const FVector& Normal, const FVector& Tangent);

}
