// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"

/**
 * UV island packing after Blender's Pack Islands operator (Shape Method: Bounding Box), written from its documented
 * behaviour, not its source.
 *
 * 1. Average Island Scale (optional, Blender's separate operator of that name): each island is scaled about its
 *    centre so every island has the same UV area per unit of surface area. The total UV area is kept.
 * 2. Rotation: None keeps each island as it is. Cardinal lets the packer turn an island by 90 degrees. Axis Aligned
 *    first turns each island so its smallest-area bounding box is axis aligned. Any does both.
 * 3. Packing: island bounding boxes are placed bottom-left on a skyline, largest first. With Scale on, the largest
 *    uniform scale that fits the unit square is found by bisection; with Scale off, islands keep their size and
 *    fill rows one unit wide, upwards.
 *
 * Margins, as Blender names them: Fraction is the exact gap between islands in the final UV space; Add is a gap in
 * the islands' own UV units, scaled with them; Scaled is Margin times the islands' overall UV size before packing,
 * also scaled with them. Half a margin is left along the edges of the unit square.
 */
namespace PCGUtilsDynMeshUVPack
{
	enum class ERotation : uint8 { None, Cardinal, AxisAligned, Any };
	enum class EMargin : uint8 { Scaled, Add, Fraction };

	struct FOptions
	{
		bool bAverageIslandScale = true;
		bool bScale = true;
		ERotation Rotation = ERotation::Any;
		EMargin MarginMethod = EMargin::Scaled;
		double Margin = 0.001;
	};

	struct FIsland
	{
		/** Indices into the UV array, each once. */
		TArray<int32> UVIndices;
		/** 3D surface area of the island's triangles; only Average Island Scale reads it. */
		double SurfaceArea = 0.0;
		/** UV area of the island's triangles; only Average Island Scale reads it. */
		double UVArea = 0.0;
	};

	struct FResult
	{
		/** Uniform scale the packed islands were given (1 with Scale off). */
		double Scale = 1.0;
		/** Islands turned by 90 degrees while packing. */
		int32 Rotated = 0;
		/** False when the islands had no area to pack. */
		bool bPacked = false;
	};

	/** Packs the islands' UVs in place. */
	PCGUTILSDYNMESH_API FResult Pack(TArray<FVector2f>& UVs, TArrayView<const FIsland> Islands, const FOptions& Options);

	struct FRegionResult
	{
		int32 Islands = 0;
		/** Triangles of the region without UVs, which were left out. */
		int32 Unset = 0;
		FResult Pack;
	};

	/** Gathers the UV islands of TriangleIDs, packs them, and writes them back (PCGUtilsDynMeshUV::WriteBack). */
	PCGUTILSDYNMESH_API FRegionResult PackRegion(const UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& Overlay,
		TArrayView<const int32> TriangleIDs, const FOptions& Options);
}
