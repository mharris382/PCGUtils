// Copyright Max Harris

#include "Voxel/PCGVoxelMorphology.h"

#include "Async/ParallelFor.h"

void FPCGVoxelDenseMask::Init(const FIntVector& Lo, const FIntVector& Hi, const FIntVector& Pad)
{
	Min = Lo - Pad;
	Dims = Hi - Lo + FIntVector(1) + Pad * 2;
	Bits.Reset();
	Bits.SetNumZeroed(Num());
}

void FPCGVoxelDenseMask::InitLike(const FPCGVoxelDenseMask& Other)
{
	Min = Other.Min;
	Dims = Other.Dims;
	Bits.Reset();
	Bits.SetNumZeroed(Num());
}

void FPCGVoxelBoxSum::Build(const FPCGVoxelDenseMask& Mask)
{
	Min = Mask.Min;
	Dims = Mask.Dims;
	Table.Reset();
	Table.SetNumZeroed(static_cast<int64>(Dims.X + 1) * (Dims.Y + 1) * (Dims.Z + 1));
	for (int32 Z = 0; Z < Dims.Z; ++Z)
		for (int32 Y = 0; Y < Dims.Y; ++Y)
			for (int32 X = 0; X < Dims.X; ++X)
			{
				const int32 Set = Mask.Bits[X + static_cast<int64>(Dims.X) * (Y + static_cast<int64>(Dims.Y) * Z)] ? 1 : 0;
				Table[At(X + 1, Y + 1, Z + 1)] = Set
					+ Table[At(X, Y + 1, Z + 1)] + Table[At(X + 1, Y, Z + 1)] + Table[At(X + 1, Y + 1, Z)]
					- Table[At(X, Y, Z + 1)] - Table[At(X, Y + 1, Z)] - Table[At(X + 1, Y, Z)]
					+ Table[At(X, Y, Z)];
			}
}

int64 FPCGVoxelBoxSum::Sum(const FIntVector& Lo, const FIntVector& Hi) const
{
	return static_cast<int64>(Table[At(Hi.X, Hi.Y, Hi.Z)])
		- Table[At(Lo.X, Hi.Y, Hi.Z)] - Table[At(Hi.X, Lo.Y, Hi.Z)] - Table[At(Hi.X, Hi.Y, Lo.Z)]
		+ Table[At(Lo.X, Lo.Y, Hi.Z)] + Table[At(Lo.X, Hi.Y, Lo.Z)] + Table[At(Hi.X, Lo.Y, Lo.Z)]
		- Table[At(Lo.X, Lo.Y, Lo.Z)];
}

int64 FPCGVoxelBoxSum::CountSet(const FIntVector& Cell, const FIntVector& Reach) const
{
	const FIntVector Local = Cell - Min;
	FIntVector Lo = Local - Reach;
	FIntVector Hi = Local + Reach + FIntVector(1);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Lo[Axis] = FMath::Clamp(Lo[Axis], 0, Dims[Axis]);
		Hi[Axis] = FMath::Clamp(Hi[Axis], 0, Dims[Axis]);
		if (Hi[Axis] <= Lo[Axis])
		{
			return 0;
		}
	}
	return Sum(Lo, Hi);
}

bool FPCGVoxelBoxSum::AllSet(const FIntVector& Cell, const FIntVector& Reach) const
{
	return AllSet(Cell, Reach, Reach);
}

bool FPCGVoxelBoxSum::AllSet(const FIntVector& Cell, const FIntVector& ReachLo, const FIntVector& ReachHi) const
{
	const FIntVector Local = Cell - Min;
	const FIntVector Lo = Local - ReachLo;
	const FIntVector Hi = Local + ReachHi + FIntVector(1);
	if (Lo.X < 0 || Lo.Y < 0 || Lo.Z < 0 || Hi.X > Dims.X || Hi.Y > Dims.Y || Hi.Z > Dims.Z)
	{
		return false;
	}
	const FIntVector Size = Hi - Lo;
	return Sum(Lo, Hi) == static_cast<int64>(Size.X) * Size.Y * Size.Z;
}

namespace PCGVoxelMorphology
{
	void Erode(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out)
	{
		FPCGVoxelBoxSum Sum;
		Sum.Build(In);
		Out.InitLike(In);
		ParallelFor(TEXT("PCGVoxelMorphology.Erode"), static_cast<int32>(In.Num()), 4096, [&](int32 I)
		{
			Out.Bits[I] = In.Bits[I] && Sum.AllSet(In.Cell(I), Reach) ? 1 : 0;
		});
	}

	void Dilate(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out)
	{
		FPCGVoxelBoxSum Sum;
		Sum.Build(In);
		Out.InitLike(In);
		ParallelFor(TEXT("PCGVoxelMorphology.Dilate"), static_cast<int32>(In.Num()), 4096, [&](int32 I)
		{
			Out.Bits[I] = In.Bits[I] || Sum.AnySet(In.Cell(I), Reach) ? 1 : 0;
		});
	}

	void Open(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out)
	{
		FPCGVoxelDenseMask Eroded;
		Erode(In, Reach, Eroded);
		Dilate(Eroded, Reach, Out);
	}

	void Close(const FPCGVoxelDenseMask& In, const FIntVector& Reach, FPCGVoxelDenseMask& Out)
	{
		FPCGVoxelDenseMask Dilated;
		Dilate(In, Reach, Dilated);
		Erode(Dilated, Reach, Out);
	}

	void FloodOutside(const FPCGVoxelDenseMask& Solid, FPCGVoxelDenseMask& Out)
	{
		static const FIntVector Faces[6] = {
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
		Out.InitLike(Solid);
		TArray<int64> Queue;
		const FIntVector& D = Solid.Dims;
		for (int64 I = 0; I < Solid.Num(); ++I)
		{
			const FIntVector L = Solid.Cell(I) - Solid.Min;
			const bool bBoundary = L.X == 0 || L.Y == 0 || L.Z == 0 || L.X == D.X - 1 || L.Y == D.Y - 1 || L.Z == D.Z - 1;
			if (bBoundary && !Solid.Bits[I])
			{
				Out.Bits[I] = 1;
				Queue.Add(I);
			}
		}
		for (int64 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FIntVector Cell = Solid.Cell(Queue[Head]);
			for (const FIntVector& Step : Faces)
			{
				const FIntVector Next = Cell + Step;
				if (!Solid.Contains(Next))
				{
					continue;
				}
				const int64 N = Solid.Index(Next);
				if (!Solid.Bits[N] && !Out.Bits[N])
				{
					Out.Bits[N] = 1;
					Queue.Add(N);
				}
			}
		}
	}
}
