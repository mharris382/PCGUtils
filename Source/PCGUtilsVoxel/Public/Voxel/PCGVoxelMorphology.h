// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

/**
 * Shared dense-grid tools for voxel nodes: a box of cells one byte each, box counts in O(1) (a summed-volume table),
 * and the morphology built on them (erode, dilate, open, close, empty cells reachable from outside). Nodes use these
 * rather than writing their own.
 *
 * Boxes are per-axis: a reach of (1, 1, 0) is a 3 x 3 x 1 box, so an axis with reach 0 is left alone (Axes settings).
 * Everything outside the dense box counts as empty.
 */

/** A dense box of cells, one byte each (non-zero = set). Cells are grid indices; Local = Cell - Min. */
struct PCGUTILSVOXEL_API FPCGVoxelDenseMask
{
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Dims = FIntVector::ZeroValue;
	TArray<uint8> Bits;

	/** Sizes the box to cover Lo..Hi (inclusive) padded by Pad on each side, all clear. */
	void Init(const FIntVector& Lo, const FIntVector& Hi, const FIntVector& Pad);
	/** Same box as Other, all clear. */
	void InitLike(const FPCGVoxelDenseMask& Other);

	int64 Num() const { return static_cast<int64>(Dims.X) * Dims.Y * Dims.Z; }
	int64 Index(const FIntVector& Cell) const
	{
		const FIntVector L = Cell - Min;
		return L.X + static_cast<int64>(Dims.X) * (L.Y + static_cast<int64>(Dims.Y) * L.Z);
	}
	bool Contains(const FIntVector& Cell) const
	{
		const FIntVector L = Cell - Min;
		return L.X >= 0 && L.Y >= 0 && L.Z >= 0 && L.X < Dims.X && L.Y < Dims.Y && L.Z < Dims.Z;
	}
	/** Set? Cells outside the box are not. */
	bool Get(const FIntVector& Cell) const { return Contains(Cell) && Bits[Index(Cell)] != 0; }
	FIntVector Cell(int64 I) const
	{
		const int64 X = I % Dims.X;
		const int64 Rest = I / Dims.X;
		return Min + FIntVector(static_cast<int32>(X), static_cast<int32>(Rest % Dims.Y), static_cast<int32>(Rest / Dims.Y));
	}
};

/** Counts of set cells over boxes of a mask in O(1). */
struct PCGUTILSVOXEL_API FPCGVoxelBoxSum
{
	void Build(const FPCGVoxelDenseMask& Mask);

	/** Set cells in the box Reach around Cell (per axis), clipped to the mask's box. */
	int64 CountSet(const FIntVector& Cell, const FIntVector& Reach) const;
	/** Every cell in the box Reach around Cell is set (false when the box leaves the mask's box). */
	bool AllSet(const FIntVector& Cell, const FIntVector& Reach) const;

	/** Whether every cell from Cell - ReachLo to Cell + ReachHi is set (false when that box leaves the dense box). */
	bool AllSet(const FIntVector& Cell, const FIntVector& ReachLo, const FIntVector& ReachHi) const;
	/** Some cell in the box Reach around Cell is set. */
	bool AnySet(const FIntVector& Cell, const FIntVector& Reach) const { return CountSet(Cell, Reach) > 0; }

private:
	int64 At(int32 X, int32 Y, int32 Z) const { return X + static_cast<int64>(Dims.X + 1) * (Y + static_cast<int64>(Dims.Y + 1) * Z); }
	int64 Sum(const FIntVector& Lo, const FIntVector& Hi) const; // local, Hi exclusive, already clipped

	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Dims = FIntVector::ZeroValue;
	TArray<int32> Table;
};

namespace PCGVoxelMorphology
{
	/** Out = cells whose whole box Reach around them is set in In. Same box as In. */
	PCGUTILSVOXEL_API void Erode(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out);
	/** Out = cells with some set cell of In within the box Reach around them. Same box as In. */
	PCGUTILSVOXEL_API void Dilate(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out);
	/** Opening: erode then dilate. Keeps what a box of 2 x Reach + 1 fits in; drops thinner protrusions. */
	PCGUTILSVOXEL_API void Open(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out);
	/** Closing: dilate then erode. Adds the gaps narrower than 2 x Reach + 1 (pad In by 2 x Reach + 1 for exact results). */
	PCGUTILSVOXEL_API void Close(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out);
	/**
	 * Out = clear cells of Solid reachable by face steps through clear cells from the box's boundary. A clear cell not
	 * in Out is an enclosed cavity (pad Solid by at least one so its outside is connected).
	 */
	PCGUTILSVOXEL_API void FloodOutside(const FPCGVoxelDenseMask& Solid, FPCGVoxelDenseMask& Out);

	/**
	 * Out = cells whose box from Cell - ReachLo to Cell + ReachHi is all set in In (an uneven erosion: an agent two
	 * cells tall standing on a cell needs ReachHi.Z = 1). A box leaving In's box is not all set. Same box as In.
	 */
	PCGUTILSVOXEL_API void ErodeBox(const FPCGVoxelDenseMask& In, const FIntVector& ReachLo, const FIntVector& ReachHi, FPCGVoxelDenseMask& Out);

	/**
	 * Out = In plus every cell whose box lies closer than RadiusOf(set cell) to that set cell's box, measured in cells
	 * (Euclidean, box to box: a face, edge or corner neighbour is 0 away, a cell two along one axis is 1 away). Radius
	 * per set cell (its dense index); 0 or less adds nothing. Only set cells with a clear face neighbour stamp, since a
	 * buried cell's reach is covered by the cells around it. Same box as In.
	 */
	PCGUTILSVOXEL_API void DilateByDistance(const FPCGVoxelDenseMask& In, TFunctionRef<double(int64 Index)> RadiusOf, FPCGVoxelDenseMask& Out);

	/** Per cell of a mask: the nearest set cell and the squared distance to its box (NearestSet). */
	struct PCGUTILSVOXEL_API FNearestSet
	{
		/** Squared distance in world units from the cell's centre to the nearest set cell's box (0 on set cells). Max where there is no set cell. */
		TArray<double> DistanceSquared;
		/** Dense index of that set cell, or INDEX_NONE when the mask has none. */
		TArray<int64> Source;
	};

	/**
	 * For every cell, the nearest set cell of In, by distance from the cell's centre to the set cell's box (VoxelSize
	 * per axis, so cells need not be cubes). Exact, in three separable passes. Equal distances are settled by
	 * AxisPriority (axis indices, most important first): the cell lying further along AxisPriority[0] wins, then
	 * AxisPriority[1], then [2]; then the one at the lower coordinate.
	 */
	PCGUTILSVOXEL_API void NearestSet(const FPCGVoxelDenseMask& In, const FVector& VoxelSize, const int32 AxisPriority[3], FNearestSet& Out);
}
