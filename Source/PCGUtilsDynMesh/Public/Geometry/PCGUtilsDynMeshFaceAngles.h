// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshDotComparison.h"

namespace UE::Geometry { class FDynamicMesh3; }

/** Geometric face-normal comparison shared by the Bevel limiter and its reusable edge selector. */
namespace PCGUtilsDynMeshFaceAngles
{
	/** Unsigned normal agreement <= cos(45 degrees): raw face turns from 45 to 135 degrees. */
	PCGUTILSDYNMESH_API FPCGUtilsDynMeshDotComparison MakeDefaultComparison();

	/** Resolve a constant or one strict @Data threshold for this mesh; errors identify the failed setting. */
	PCGUTILSDYNMESH_API bool ResolveComparison(const FPCGUtilsDynMeshDotComparison& Comparison,
		const UPCGData* MeshData, double& OutThresholdDot, FText& OutError);

	/** Boundary edges and edges incident to a degenerate face cannot pass a two-face comparison. */
	PCGUTILSDYNMESH_API bool TestEdge(const UE::Geometry::FDynamicMesh3& Mesh, int32 EdgeID,
		const FPCGUtilsDynMeshDotComparison& Comparison, double ThresholdDot);
}
