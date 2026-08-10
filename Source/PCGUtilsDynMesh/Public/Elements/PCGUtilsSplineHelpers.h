#pragma once

#include "CoreMinimal.h"

class UPCGSplineData;
struct FPCGContext;

/**
 * Small shared helpers for PCGUtilsDynMesh nodes that combine PCG spline data with Dynamic Mesh data (Spline
 * Deform, Selection From Spline). Deliberately minimal - not a general spline framework.
 */
namespace PCGUtilsSplineHelpers
{
	/**
	 * Resolves exactly one UPCGSplineData object connected to SplinePinLabel. Logs a graph error and returns
	 * nullptr if zero or more than one spline data object is present (V1 requires exactly one).
	 */
	PCGUTILSDYNMESH_API const UPCGSplineData* ResolveSingleSpline(FPCGContext* Context, FName SplinePinLabel);

	/**
	 * Resolves the PCG target actor's world transform, for converting between the spline's native World-space
	 * evaluation and the coordinate space Dynamic Mesh data is expected to be in - matching the established
	 * PCGSplineToDynamicMesh convention (world-space spline data, target-actor-local Dynamic Mesh data).
	 *
	 * If bConvertToLocalSpace is false, returns FTransform::Identity (callers should then treat spline World-space
	 * evaluations as already being in the mesh's coordinate space). If true but no target actor can be resolved,
	 * logs a warning and also returns Identity, so callers degrade gracefully rather than producing garbage output.
	 *
	 * Callers apply the returned transform themselves, in whichever direction their computation needs:
	 *   - mesh-local position -> world:      ActorTransform.TransformPosition(LocalPos)
	 *   - world-space frame -> mesh-local:   WorldFrame.GetRelativeTransform(ActorTransform)
	 *   - world-space direction -> mesh-local: ActorTransform.InverseTransformVectorNoScale(WorldDir)
	 */
	PCGUTILSDYNMESH_API FTransform ResolveActorTransformForSpline(
		FPCGContext* Context, const UPCGSplineData* SplineData, bool bConvertToLocalSpace);
}
