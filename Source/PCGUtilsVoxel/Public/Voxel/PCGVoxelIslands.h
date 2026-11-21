// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Voxel/PCGVoxelGrid.h"

/**
 * The connected parts (islands) of a voxel set. Shared by every node that needs islands (Separate Islands, Cell
 * Properties' component outputs); no node labels islands on its own.
 *
 * Islands are numbered from 0 in cell order (Z, then Y, then X, by each island's first cell), so the numbering does
 * not depend on point order or on how the union ran. A chunk is one box, so all its cells are always in one island.
 */
struct PCGUTILSVOXEL_API FPCGVoxelIslands
{
	/** Per entry of FPCGVoxelGrid::Cells: its island. */
	TArray<int32> CellIsland;

	/**
	 * Per point: its island. A point Duplicate Cells ignored joins the island of the cell at its first cell (it was
	 * written for that cell); INDEX_NONE when that cell is empty.
	 */
	TArray<int32> PointIsland;

	/** Per island: how many cells it has, and its lowest and highest cell on each axis. */
	TArray<int32> CellCount;
	TArray<FIntVector> MinCell;
	TArray<FIntVector> MaxCell;

	int32 Num() const { return CellCount.Num(); }
};

namespace PCGVoxel
{
	/** Neighbour steps for a connectivity, one of each opposite pair (first non-zero component positive). */
	PCGUTILSVOXEL_API TArray<FIntVector> HalfNeighbourSteps(EPCGVoxelConnectivity Connectivity);

	/** Labels the islands of Grid: cells that touch under Connectivity are in one island. */
	PCGUTILSVOXEL_API void FindIslands(const FPCGVoxelGrid& Grid, EPCGVoxelConnectivity Connectivity, FPCGVoxelIslands& OutIslands);
}
