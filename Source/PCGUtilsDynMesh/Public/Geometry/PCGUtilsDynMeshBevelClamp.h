// Copyright Max Harris

#pragma once

#include "Geometry/PCGUtilsDynMeshAmountClamp.h"

namespace UE::Geometry { class FDynamicMesh3; }

namespace PCGUtilsDynMeshBevelClamp
{
	struct FOptions
	{
		double Distance = 1.0;
		double MinimumDistance = 0.001;
		EPCGUtilsDynMeshAmountClampMode Mode = EPCGUtilsDynMeshAmountClampMode::Regional;
		int32 Subdivisions = 0;
		double RoundWeight = 1.0;
		bool bInferMaterialID = false;
		int32 MaterialID = 0;
	};

	using FResult = PCGUtilsDynMeshAmountClamp::FResult;

	/**
	 * Adapter from the engine Bevel to the shared amount-clamping library, using the requested clamp mode.
	 * Validate the complete result for new intersections and back off if necessary. Failed candidates never
	 * modify Mesh. If no usable width exists, Mesh passes through and OutTriangles is empty.
	 */
	PCGUTILSDYNMESH_API bool Apply(UE::Geometry::FDynamicMesh3& Mesh, TConstArrayView<int32> Edges,
		const FOptions& Options, TArray<int32>& OutTriangles, FResult* OutResult = nullptr);
}
