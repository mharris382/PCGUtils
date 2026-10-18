// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Builds the Painter sample for a canonical-mesh vertex: local/world position and local/world normal.
 *
 * This is the one definition of a sample. The shared paint traversal uses it for every vertex it writes, and a
 * Painter that has to evaluate its input away from the sample it was asked about (Painter | Blur) uses it too,
 * so both see exactly the same field.
 *
 * Normals prefer averaged overlay normals, then baked per-vertex normals, then +Z. World normals use the
 * inverse-transpose of LocalToWorld, so non-uniform scale is handled.
 */
class PCGUTILSPAINTER_API FPCGUtilsPainterSampleBuilder
{
public:
	FPCGUtilsPainterSampleBuilder(const UE::Geometry::FDynamicMesh3& InMesh, const FTransform& InLocalToWorld);

	FPCGUtilsDynMeshPainterSample MakeSample(int32 VertexID) const;

private:
	const UE::Geometry::FDynamicMesh3* Mesh = nullptr;
	FTransform LocalToWorld = FTransform::Identity;
	FMatrix NormalToWorld = FMatrix::Identity;
	/** Averaged overlay normals indexed by vertex ID; empty when the mesh has no overlay normals. */
	TArray<FVector3d> OverlayNormals;
};
