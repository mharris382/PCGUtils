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

namespace PCGVoxelMorphology
{
	void ErodeBox(const FPCGVoxelDenseMask& In, const FIntVector& ReachLo, const FIntVector& ReachHi, FPCGVoxelDenseMask& Out)
	{
		FPCGVoxelBoxSum Sum;
		Sum.Build(In);
		Out.InitLike(In);
		ParallelFor(TEXT("PCGVoxelMorphology.ErodeBox"), static_cast<int32>(In.Num()), 4096, [&](int32 I)
		{
			Out.Bits[I] = In.Bits[I] && Sum.AllSet(In.Cell(I), ReachLo, ReachHi) ? 1 : 0;
		});
	}

	void DilateByDistance(const FPCGVoxelDenseMask& In, TFunctionRef<double(int64 Index)> RadiusOf, FPCGVoxelDenseMask& Out)
	{
		static const FIntVector Faces[6] = {
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
		Out = In;
		// One stamp of offsets per distinct radius: every d with sum over axes of max(0, |d| - 1)^2 < Radius^2.
		TMap<double, TArray<FIntVector>> Stamps;
		auto StampOf = [&](double Radius) -> const TArray<FIntVector>&
		{
			if (const TArray<FIntVector>* Found = Stamps.Find(Radius))
			{
				return *Found;
			}
			TArray<FIntVector> Offsets;
			const int32 Reach = FMath::CeilToInt32(Radius) + 1;
			const double RadiusSquared = Radius * Radius;
			for (int32 Z = -Reach; Z <= Reach; ++Z)
				for (int32 Y = -Reach; Y <= Reach; ++Y)
					for (int32 X = -Reach; X <= Reach; ++X)
					{
						const double GX = FMath::Max(0, FMath::Abs(X) - 1);
						const double GY = FMath::Max(0, FMath::Abs(Y) - 1);
						const double GZ = FMath::Max(0, FMath::Abs(Z) - 1);
						if ((X | Y | Z) != 0 && GX * GX + GY * GY + GZ * GZ < RadiusSquared)
						{
							Offsets.Add(FIntVector(X, Y, Z));
						}
					}
			return Stamps.Add(Radius, MoveTemp(Offsets));
		};
		for (int64 I = 0; I < In.Num(); ++I)
		{
			if (!In.Bits[I])
			{
				continue;
			}
			const double Radius = RadiusOf(I);
			if (!(Radius > 0.0))
			{
				continue;
			}
			const FIntVector Cell = In.Cell(I);
			bool bExposed = false;
			for (const FIntVector& Step : Faces)
			{
				bExposed |= !In.Get(Cell + Step);
			}
			if (!bExposed)
			{
				continue;
			}
			for (const FIntVector& Offset : StampOf(Radius))
			{
				const FIntVector Other = Cell + Offset;
				if (Out.Contains(Other))
				{
					Out.Bits[Out.Index(Other)] = 1;
				}
			}
		}
	}

	void NearestSet(const FPCGVoxelDenseMask& In, const FVector& VoxelSize, const int32 AxisPriority[3], FNearestSet& Out)
	{
		const int64 Num = In.Num();
		constexpr double Far = TNumericLimits<double>::Max();
		Out.DistanceSquared.Init(Far, Num);
		Out.Source.Init(INDEX_NONE, Num);
		if (Num == 0)
		{
			return;
		}
		// Per axis cost of a k-cell offset, centre to box: (|k| - 0.5) cells, squared, or 0 on the same row.
		auto Cost = [&](int32 Axis, int32 K)
		{
			if (K == 0)
			{
				return 0.0;
			}
			const double Cells = FMath::Abs(K) - 0.5;
			return Cells * Cells * VoxelSize[Axis] * VoxelSize[Axis];
		};
		// Of two sources at the same distance from Cell, whether A should win.
		auto Prefer = [&](int64 Cell, int64 A, int64 B)
		{
			const FIntVector C = In.Cell(Cell);
			const FIntVector DA = In.Cell(A) - C;
			const FIntVector DB = In.Cell(B) - C;
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Axis = AxisPriority[K];
				if (FMath::Abs(DA[Axis]) != FMath::Abs(DB[Axis]))
				{
					return FMath::Abs(DA[Axis]) > FMath::Abs(DB[Axis]);
				}
			}
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Axis = AxisPriority[K];
				if (DA[Axis] != DB[Axis])
				{
					return DA[Axis] < DB[Axis];
				}
			}
			return false;
		};
		const FIntVector Dims = In.Dims;
		auto Stride = [&](int32 Axis) -> int64 { return Axis == 0 ? 1 : (Axis == 1 ? Dims.X : static_cast<int64>(Dims.X) * Dims.Y); };

		for (int64 I = 0; I < Num; ++I)
		{
			if (In.Bits[I])
			{
				Out.DistanceSquared[I] = 0.0;
				Out.Source[I] = I;
			}
		}
		// One pass per axis: each cell takes the best of the cells on its line plus the cost of the offset along it.
		// Lines are independent. The scan stops once the offset alone costs more than the best found.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 Length = Dims[Axis];
			const int64 Step = Stride(Axis);
			const int32 A1 = (Axis + 1) % 3;
			const int32 A2 = (Axis + 2) % 3;
			const int32 NumLines = Dims[A1] * Dims[A2];
			TArray<double> NewDistance = Out.DistanceSquared;
			TArray<int64> NewSource = Out.Source;
			ParallelFor(TEXT("PCGVoxelMorphology.NearestSet"), NumLines, 64, [&](int32 Line)
			{
				FIntVector L(0);
				L[A1] = Line % Dims[A1];
				L[A2] = Line / Dims[A1];
				const int64 Base = L.X + static_cast<int64>(Dims.X) * (L.Y + static_cast<int64>(Dims.Y) * L.Z);
				for (int32 I = 0; I < Length; ++I)
				{
					const int64 Cell = Base + I * Step;
					double Best = Out.DistanceSquared[Cell];
					int64 BestSource = Out.Source[Cell];
					for (int32 K = 1; K < Length; ++K)
					{
						const double Offset = Cost(Axis, K);
						if (Offset > Best)
						{
							break;
						}
						for (const int32 J : { I - K, I + K })
						{
							if (J < 0 || J >= Length)
							{
								continue;
							}
							const int64 Other = Base + J * Step;
							if (Out.Source[Other] == INDEX_NONE)
							{
								continue;
							}
							const double D = Out.DistanceSquared[Other] + Offset;
							if (D < Best || (D == Best && BestSource != INDEX_NONE && Prefer(Cell, Out.Source[Other], BestSource)))
							{
								Best = D;
								BestSource = Out.Source[Other];
							}
						}
					}
					NewDistance[Cell] = Best;
					NewSource[Cell] = BestSource;
				}
			});
			Out.DistanceSquared = MoveTemp(NewDistance);
			Out.Source = MoveTemp(NewSource);
		}
	}
}
