// Copyright Max Harris

#include "Voxel/PCGVoxelIslands.h"

namespace PCGVoxelIslandsPrivate
{
	struct FUnionFind
	{
		TArray<int32> Parent;
		explicit FUnionFind(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Parent[Index] = Index;
			}
		}
		int32 Find(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};
}

TArray<FIntVector> PCGVoxel::HalfNeighbourSteps(EPCGVoxelConnectivity Connectivity)
{
	const int32 MaxNonZero = Connectivity == EPCGVoxelConnectivity::Faces ? 1 : (Connectivity == EPCGVoxelConnectivity::Edges ? 2 : 3);
	TArray<FIntVector> Steps;
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				const int32 NonZero = (X != 0) + (Y != 0) + (Z != 0);
				const int32 FirstNonZero = X != 0 ? X : (Y != 0 ? Y : Z);
				if (NonZero > 0 && NonZero <= MaxNonZero && FirstNonZero > 0)
				{
					Steps.Add(FIntVector(X, Y, Z));
				}
			}
		}
	}
	return Steps;
}

void PCGVoxel::FindIslands(const FPCGVoxelGrid& Grid, EPCGVoxelConnectivity Connectivity, FPCGVoxelIslands& OutIslands)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGVoxel::FindIslands);
	using namespace PCGVoxelIslandsPrivate;

	OutIslands = FPCGVoxelIslands();
	const int32 NumCells = Grid.NumCells();

	const TArray<FIntVector> Steps = HalfNeighbourSteps(Connectivity);
	FUnionFind Sets(NumCells);
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		const FIntVector& C = Grid.Cells[Cell];
		for (const FIntVector& Step : Steps)
		{
			const int32 Other = Grid.FindCellIndex(C + Step);
			if (Other != INDEX_NONE)
			{
				Sets.Union(Cell, Other);
			}
		}
	}

	// Numbered in cell order (Cells is sorted Z, Y, X), so the result does not depend on the union order.
	TArray<int32> RootIsland;
	RootIsland.Init(INDEX_NONE, NumCells);
	OutIslands.CellIsland.SetNumUninitialized(NumCells);
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		const int32 Root = Sets.Find(Cell);
		const FIntVector& C = Grid.Cells[Cell];
		if (RootIsland[Root] == INDEX_NONE)
		{
			RootIsland[Root] = OutIslands.CellCount.Add(0);
			OutIslands.MinCell.Add(C);
			OutIslands.MaxCell.Add(C);
		}
		const int32 Island = RootIsland[Root];
		OutIslands.CellIsland[Cell] = Island;
		++OutIslands.CellCount[Island];
		OutIslands.MinCell[Island] = FIntVector(
			FMath::Min(OutIslands.MinCell[Island].X, C.X), FMath::Min(OutIslands.MinCell[Island].Y, C.Y), FMath::Min(OutIslands.MinCell[Island].Z, C.Z));
		OutIslands.MaxCell[Island] = FIntVector(
			FMath::Max(OutIslands.MaxCell[Island].X, C.X), FMath::Max(OutIslands.MaxCell[Island].Y, C.Y), FMath::Max(OutIslands.MaxCell[Island].Z, C.Z));
	}

	// Points: a used point takes its cells' island; an ignored one the island of the cell at its first cell.
	const int32 NumPoints = Grid.NumPoints();
	OutIslands.PointIsland.Init(INDEX_NONE, NumPoints);
	for (int32 Point = 0; Point < NumPoints; ++Point)
	{
		const int32 Cell = Grid.FindCellIndex(Grid.PointFirst[Point]);
		if (Cell != INDEX_NONE)
		{
			OutIslands.PointIsland[Point] = OutIslands.CellIsland[Cell];
		}
	}
}
