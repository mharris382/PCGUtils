// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Metadata/PCGMetadataAttribute.h"

#include "PCGVoxelChunking.generated.h"

class UPCGBasePointData;
struct FPCGVoxelGrid;

/**
 * Shared compression (chunking) for voxel nodes: which cells may share a chunk, and the merges that make chunks
 * (aligned octree cubes, boxes joined along the axes, random tilings). Voxel | Compress and Voxel | Offset Surface both
 * use these; no node writes its own (designer rule, 2026-11-15).
 */

/** What shapes cells are merged into. */
UENUM(BlueprintType)
enum class EPCGVoxelChunkShape : uint8
{
	/** N x N x N chunks on an aligned octree: power-of-two cubes at multiples of their size. */
	Cubes,
	/** Cubes first, then neighbouring equal chunks joined along X, Y, then Z into boxes. Usually fewer points. */
	Boxes,
	/**
	 * Boxes of random size, for looks rather than fewer points: a staggered tiling like a wall of uneven slabs. Cells
	 * are visited in order (Z, then Y, then X); each cell not yet taken starts a box sized by Random Size, Random Span
	 * and Random Chance, read from that start cell. Uses the node seed.
	 */
	Random,
};

/** Shape Random: what size each box may be. */
UENUM(BlueprintType)
enum class EPCGVoxelRandomSize : uint8
{
	/** Each box draws a size from 1 to Random Span per axis and grows as far as free, equal cells allow. */
	UpTo UMETA(DisplayName="Up To"),
	/** Each box is exactly Random Span, or its start cell stays a single cell when that box does not fit. */
	Exact,
	/**
	 * Each box is a cube: it draws a side from 1 to the smallest axis of Random Span and becomes the largest cube up to
	 * that side that fits.
	 */
	Cubes UMETA(DisplayName="Cubes Up To"),
};

/** Which values two cells must share to be merged. */
UENUM(BlueprintType)
enum class EPCGVoxelMatchAttributes : uint8
{
	/** Every point attribute plus $Color, $Density and $Steepness. Nothing is lost. */
	All,
	/** Only the attributes in Listed Attributes. Cells keep only the first cell's values for everything else. */
	Listed,
};

/** Whether two points of one point data may share a chunk: equal compared values, and equal surface offsets when set. */
struct PCGUTILSVOXEL_API FPCGVoxelPointEquality
{
	TArray<const FPCGMetadataAttributeBase*> Attributes;
	bool bColor = false;
	bool bDensity = false;
	bool bSteepness = false;
	TConstPCGValueRange<int64> Entries;
	TConstPCGValueRange<FVector4> Colors;
	TConstPCGValueRange<float> Densities;
	TConstPCGValueRange<float> Steepnesses;
	/** Per point surface offsets (FPCGVoxelGrid::ComputeSurfaceOffsets); left empty, offsets are not compared. */
	TArray<FVector> OffsetMin;
	TArray<FVector> OffsetMax;

	/**
	 * Sets up the compared values. All: every point attribute except SpanAttribute and SurroundedAttribute (they
	 * describe the box, not the cell), plus $Color, $Density and $Steepness. Listed: those names ($Color, $Density,
	 * $Steepness or point attributes). Returns false with OutMissing set when a listed name is not a point attribute.
	 */
	bool Init(const UPCGBasePointData* Points, EPCGVoxelMatchAttributes Match, TConstArrayView<FName> Listed,
		FName SpanAttribute, FName SurroundedAttribute, FName& OutMissing);

	bool operator()(int32 A, int32 B) const;
};

namespace PCGVoxelChunking
{
	/** One chunk: a box of cells and the label (point, piece, ...) it copies. */
	struct FBox
	{
		FIntVector First = FIntVector::ZeroValue;
		FIntVector Span = FIntVector(1);
		int32 Rep = INDEX_NONE;
	};

	/** May the cells labelled A and B share a chunk? */
	using FEqual = TFunctionRef<bool(int32 A, int32 B)>;

	/**
	 * Merges cells into chunks: aligned octree cubes up to MaxSpan, then (bJoinBoxes) neighbouring equal chunks joined
	 * along X, Y, then Z where their other extents match. Cells[i] is labelled Labels[i]; only cells in the list are
	 * merged, so leave out any cell that must stay alone. Appends to Out, sorted by first cell (Z, Y, X).
	 */
	PCGUTILSVOXEL_API void MergeCells(TConstArrayView<FIntVector> Cells, TConstArrayView<int32> Labels, int32 MaxSpan, bool bJoinBoxes,
		FEqual Equal, TArray<FBox>& Out);

	/**
	 * Greedy boxes, unaligned: cells visited in order (Z, then Y, then X); each cell not yet taken starts a box that grows
	 * along X, then Y (whole rows), then Z (whole slabs), only over listed cells that are not taken and equal to the start
	 * cell, up to MaxSpanOf(start label) on each axis. Fewer boxes than MergeCells where the region is not aligned to the
	 * octree (rooms, corridors). Appends to Out, sorted by first cell.
	 */
	PCGUTILSVOXEL_API void GrowBoxes(TConstArrayView<FIntVector> Cells, TConstArrayView<int32> Labels,
		TFunctionRef<FIntVector(int32 Label)> MaxSpanOf, FEqual Equal, TArray<FBox>& Out);

	/** Joins neighbouring equal boxes along Axis where their other two extents match, up to MaxSpan along Axis. */
	PCGUTILSVOXEL_API void JoinAlong(TArray<FBox>& Boxes, int32 Axis, int32 MaxSpan, FEqual Equal);

	/** Shape Random's per-start-cell inputs: the size mode, each point's span and chance. */
	struct FRandomPlan
	{
		EPCGVoxelRandomSize Mode = EPCGVoxelRandomSize::UpTo;
		FIntVector SingleSpan = FIntVector(3);
		TArray<FIntVector> PointSpan; // empty: SingleSpan for every point
		TPCGUtilsPointValues<double> Chance;

		const FIntVector& SpanOf(int32 Point) const { return PointSpan.IsEmpty() ? SingleSpan : PointSpan[Point]; }
	};

	/**
	 * Random tiling of a grid's cells (labels are points): cells in Z, Y, X order; each cell not yet taken starts a box,
	 * sized by the plan from that start cell's span and chance, and grown only over cells that are occupied, free and
	 * equal to the start cell. Appends to Out.
	 */
	PCGUTILSVOXEL_API void RandomBoxes(const FPCGVoxelGrid& Grid, const FRandomPlan& Plan, int32 Seed, FEqual Equal, TArray<FBox>& Out);
}
