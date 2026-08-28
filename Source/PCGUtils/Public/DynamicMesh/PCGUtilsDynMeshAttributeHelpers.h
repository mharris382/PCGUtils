// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
	template<typename RealType, int ElementSize, typename VectorType>
	class TDynamicMeshVectorOverlay;
	using FDynamicMeshColorOverlay = TDynamicMeshVectorOverlay<float, 4, FVector4f>;
}

/** Shared seam-aware helpers for reading and writing DynMesh color overlays by geometric vertex. */
namespace PCGUtilsDynMeshAttributeHelpers
{
	/** Ensures a primary color overlay exists, initializing a seamless overlay when it is empty. */
	PCGUTILS_API UE::Geometry::FDynamicMeshColorOverlay* EnsurePrimaryColorOverlay(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector4f& InitialColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f));

	/** Reads the first valid color element attached to a geometric vertex. */
	PCGUTILS_API FVector4f GetVertexColor(
		const UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FDynamicMeshColorOverlay& Overlay,
		int32 VertexID,
		const FVector4f& DefaultColor);

	/** Writes the same color to every split/seam overlay element attached to a geometric vertex. */
	PCGUTILS_API void SetVertexColor(
		UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshColorOverlay& Overlay,
		int32 VertexID,
		const FVector4f& Color);
}
