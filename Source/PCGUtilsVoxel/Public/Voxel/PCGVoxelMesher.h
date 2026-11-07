// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGVoxelMesher.generated.h"

struct FPCGVoxelGrid;
namespace UE::Geometry { class FDynamicMesh3; }

/** How vertex colors change where neighbouring cells on the surface have different colors. */
UENUM(BlueprintType)
enum class EPCGVoxelColorTransition : uint8
{
	/** Each face has its cell's exact color. Positions stay shared; only the color is split. */
	Hard,
	/** Each surface corner takes the average color of the cells meeting there, giving gradients across cells. */
	Smooth,
	/** As Smooth, but averaging stops at material boundaries, which stay hard. */
	SmoothWithinMaterial UMETA(DisplayName="Smooth Within Material"),
};

/** How a voxel mesh gets UVs. */
UENUM(BlueprintType)
enum class EPCGVoxelUVMode : uint8
{
	None,
	/** Each face projected on its own axis in grid space, UV Scale units per cell. Seamless across cells and collapsed faces. */
	GridProjection UMETA(DisplayName="Grid Projection"),
	/** Each cell face spans 0..1; a collapsed face spans 0..N, so a tiling texture repeats once per cell. */
	PerFace UMETA(DisplayName="Per Face"),
	/** Per Face UVs mapped into the cell's tile of a texture atlas. Faces are never collapsed in this mode. */
	AtlasFromAttribute UMETA(DisplayName="Atlas From Attribute"),
};

/** Which polygroup each face of a voxel mesh gets. */
UENUM(BlueprintType)
enum class EPCGVoxelPolygroupMode : uint8
{
	None,
	/** One group per face direction (+X, -X, +Y, -Y, +Z, -Z). */
	PerFaceDirection UMETA(DisplayName="Per Face Direction"),
	/** One group per material slot. */
	PerMaterial UMETA(DisplayName="Per Material"),
	/** One group per collapsed face (per cell face when Collapse Coplanar Faces is off). */
	PerCollapsedRegion UMETA(DisplayName="Per Collapsed Region"),
};

/** Per-point values a voxel mesh is built from. Each array is either empty (not used) or one value per grid point. */
struct PCGUTILSVOXEL_API FPCGVoxelMeshInput
{
	const FPCGVoxelGrid* Grid = nullptr;

	/** Material slot of each point's faces. Empty: every face uses slot 0. */
	TArray<int32> MaterialSlot;

	/** Color of each point. Empty: the mesh has no vertex colors. */
	TArray<FVector4f> Color;

	/** Atlas tile of each point as (MinU, MinV, MaxU, MaxV). Used by Atlas From Attribute only. */
	TArray<FVector4f> AtlasRect;

	/**
	 * How far each point's surface moves outward on its -X/-Y/-Z sides (OffsetMin) and +X/+Y/+Z sides (OffsetMax).
	 * Empty: no offset. Every value must be greater than minus half the point's extent on that axis.
	 */
	TArray<FVector> OffsetMin;
	TArray<FVector> OffsetMax;
};

struct PCGUTILSVOXEL_API FPCGVoxelMeshOptions
{
	bool bCollapseCoplanarFaces = false;
	EPCGVoxelColorTransition ColorTransition = EPCGVoxelColorTransition::Hard;
	EPCGVoxelUVMode UVMode = EPCGVoxelUVMode::GridProjection;
	double UVScale = 1.0;
	int32 UVChannel = 0;
	EPCGVoxelPolygroupMode Polygroups = EPCGVoxelPolygroupMode::PerFaceDirection;

	/** The space vertices are written in: output = OutputSpace.Inverse(GridToWorld(grid position)). Identity = world. */
	FTransform OutputSpace = FTransform::Identity;
};

struct PCGUTILSVOXEL_API FPCGVoxelMeshStats
{
	int32 NumFaces = 0;
	int32 NumRegions = 0;

	/** Seconds spent in each stage, for profiling: faces, vertices, offsets, smooth colors, regions, emit, normals. */
	static constexpr int32 NumStages = 7;
	double StageSeconds[NumStages] = {};
	static const TCHAR* StageName(int32 Stage)
	{
		static const TCHAR* Names[NumStages] = { TEXT("faces"), TEXT("vertices"), TEXT("offsets"), TEXT("colors"), TEXT("regions"), TEXT("emit"), TEXT("normals") };
		return Names[Stage];
	}
};

namespace PCGVoxelMesher
{
	/**
	 * Builds the outer surface of a voxel set: a face for every cell side whose neighbour is empty, vertices shared
	 * so the result is closed, and split where cells touch only along an edge or at a corner so it stays manifold.
	 * Normals are flat with hard edges between face directions.
	 */
	PCGUTILSVOXEL_API void BuildMesh(const FPCGVoxelMeshInput& Input, const FPCGVoxelMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& OutMesh, FPCGVoxelMeshStats* OutStats = nullptr);
}
