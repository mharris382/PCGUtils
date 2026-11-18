// Copyright Max Harris

#include "Voxel/PCGVoxelChunking.h"

#include "Data/PCGBasePointData.h"
#include "Math/RandomStream.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelChunkingPrivate
{
	/** Rounds down to a multiple of Size, also for negative values. */
	int32 FloorAlign(int32 Value, int32 Size)
	{
		return (Value >= 0 ? Value / Size : -((-Value + Size - 1) / Size)) * Size;
	}

	FIntVector FloorAlign(const FIntVector& Cell, int32 Size)
	{
		return FIntVector(FloorAlign(Cell.X, Size), FloorAlign(Cell.Y, Size), FloorAlign(Cell.Z, Size));
	}
}

bool FPCGVoxelPointEquality::Init(const UPCGBasePointData* Points, EPCGVoxelMatchAttributes Match, TConstArrayView<FName> Listed,
	FName SpanAttribute, FName SurroundedAttribute, FName& OutMissing)
{
	Entries = Points->GetConstMetadataEntryValueRange();
	Colors = Points->GetConstColorValueRange();
	Densities = Points->GetConstDensityValueRange();
	Steepnesses = Points->GetConstSteepnessValueRange();
	Attributes.Reset();
	const FPCGMetadataDomain* Domain = Points->ConstMetadata() ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
	if (Match == EPCGVoxelMatchAttributes::All)
	{
		bColor = bDensity = bSteepness = true;
		if (Domain)
		{
			TArray<FName> Names;
			TArray<EPCGMetadataTypes> Types;
			Domain->GetAttributes(Names, Types);
			for (const FName Name : Names)
			{
				if (Name != SpanAttribute && Name != SurroundedAttribute)
				{
					Attributes.Add(Domain->GetConstAttribute(Name));
				}
			}
		}
		return true;
	}
	bColor = bDensity = bSteepness = false;
	for (const FName Name : Listed)
	{
		if (Name == TEXT("$Color")) { bColor = true; continue; }
		if (Name == TEXT("$Density")) { bDensity = true; continue; }
		if (Name == TEXT("$Steepness")) { bSteepness = true; continue; }
		const FPCGMetadataAttributeBase* Attribute = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Attribute)
		{
			OutMissing = Name;
			return false;
		}
		Attributes.Add(Attribute);
	}
	return true;
}

bool FPCGVoxelPointEquality::operator()(int32 A, int32 B) const
{
	if (A == B)
	{
		return true;
	}
	if (!OffsetMin.IsEmpty() && (!OffsetMin[A].Equals(OffsetMin[B], 1e-6) || !OffsetMax[A].Equals(OffsetMax[B], 1e-6)))
	{
		return false;
	}
	if ((bColor && Colors[A] != Colors[B]) || (bDensity && Densities[A] != Densities[B]) || (bSteepness && Steepnesses[A] != Steepnesses[B]))
	{
		return false;
	}
	for (const FPCGMetadataAttributeBase* Attribute : Attributes)
	{
		if (!Attribute->AreValuesEqualForEntryKeys(Entries[A], Entries[B]))
		{
			return false;
		}
	}
	return true;
}

namespace PCGVoxelChunking
{
	void JoinAlong(TArray<FBox>& Boxes, int32 Axis, int32 MaxSpan, FEqual Equal)
	{
		const int32 A1 = (Axis + 1) % 3;
		const int32 A2 = (Axis + 2) % 3;
		Boxes.Sort([&](const FBox& L, const FBox& R)
		{
			if (L.First[A2] != R.First[A2]) return L.First[A2] < R.First[A2];
			if (L.First[A1] != R.First[A1]) return L.First[A1] < R.First[A1];
			if (L.Span[A2] != R.Span[A2]) return L.Span[A2] < R.Span[A2];
			if (L.Span[A1] != R.Span[A1]) return L.Span[A1] < R.Span[A1];
			return L.First[Axis] < R.First[Axis];
		});
		TArray<FBox> Joined;
		Joined.Reserve(Boxes.Num());
		for (const FBox& Box : Boxes)
		{
			if (!Joined.IsEmpty())
			{
				FBox& Last = Joined.Last();
				const bool bSameRow = Last.First[A1] == Box.First[A1] && Last.First[A2] == Box.First[A2]
					&& Last.Span[A1] == Box.Span[A1] && Last.Span[A2] == Box.Span[A2];
				if (bSameRow && Last.First[Axis] + Last.Span[Axis] == Box.First[Axis]
					&& Last.Span[Axis] + Box.Span[Axis] <= MaxSpan && Equal(Last.Rep, Box.Rep))
				{
					Last.Span[Axis] += Box.Span[Axis];
					continue;
				}
			}
			Joined.Add(Box);
		}
		Boxes = MoveTemp(Joined);
	}

	void MergeCells(TConstArrayView<FIntVector> Cells, TConstArrayView<int32> Labels, int32 MaxSpan, bool bJoinBoxes,
		FEqual Equal, TArray<FBox>& Out)
	{
		using namespace PCGVoxelChunkingPrivate;
		check(Cells.Num() == Labels.Num());
		MaxSpan = FMath::Max(1, MaxSpan);

		// Aligned octree: eight equal full cubes of size S at a multiple of 2S become one cube of size 2S.
		TArray<FBox> Boxes;
		TMap<FIntVector, int32> Level;
		Level.Reserve(Cells.Num());
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Level.Add(Cells[Index], Labels[Index]);
		}
		int32 Size = 1;
		while (Size * 2 <= MaxSpan && !Level.IsEmpty())
		{
			TMap<FIntVector, int32> ChildCount;
			for (const TPair<FIntVector, int32>& Node : Level)
			{
				++ChildCount.FindOrAdd(FloorAlign(Node.Key, Size * 2));
			}
			TMap<FIntVector, int32> Next;
			TSet<FIntVector> Merged;
			for (const TPair<FIntVector, int32>& Parent : ChildCount)
			{
				if (Parent.Value != 8)
				{
					continue;
				}
				const int32 Rep = Level.FindChecked(Parent.Key);
				bool bEqual = true;
				for (int32 Child = 1; Child < 8 && bEqual; ++Child)
				{
					const FIntVector Offset((Child & 1) ? Size : 0, (Child & 2) ? Size : 0, (Child & 4) ? Size : 0);
					bEqual = Equal(Rep, Level.FindChecked(Parent.Key + Offset));
				}
				if (bEqual)
				{
					Next.Add(Parent.Key, Rep);
					Merged.Add(Parent.Key);
				}
			}
			for (const TPair<FIntVector, int32>& Node : Level)
			{
				if (!Merged.Contains(FloorAlign(Node.Key, Size * 2)))
				{
					Boxes.Add({ Node.Key, FIntVector(Size), Node.Value });
				}
			}
			Level = MoveTemp(Next);
			Size *= 2;
		}
		for (const TPair<FIntVector, int32>& Node : Level)
		{
			Boxes.Add({ Node.Key, FIntVector(Size), Node.Value });
		}

		if (bJoinBoxes)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				JoinAlong(Boxes, Axis, MaxSpan, Equal);
			}
		}
		Boxes.Sort([](const FBox& A, const FBox& B) { return FPCGVoxelGrid::CellLess(A.First, B.First); });
		Out.Append(MoveTemp(Boxes));
	}

	void GrowBoxes(TConstArrayView<FIntVector> Cells, TConstArrayView<int32> Labels,
		TFunctionRef<FIntVector(int32 Label)> MaxSpanOf, FEqual Equal, TArray<FBox>& Out)
	{
		check(Cells.Num() == Labels.Num());
		TMap<FIntVector, int32> IndexOf;
		IndexOf.Reserve(Cells.Num());
		TArray<int32> Order;
		Order.Reserve(Cells.Num());
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			if (!IndexOf.Contains(Cells[Index]))
			{
				IndexOf.Add(Cells[Index], Index);
				Order.Add(Index);
			}
		}
		Order.Sort([&](int32 A, int32 B) { return FPCGVoxelGrid::CellLess(Cells[A], Cells[B]); });
		TArray<bool> Taken;
		Taken.Init(false, Cells.Num());

		// Is every cell of the box from Lo spanning Size listed, free and equal to Rep?
		auto BlockFree = [&](const FIntVector& Lo, const FIntVector& Size, int32 Rep)
		{
			for (int32 Z = 0; Z < Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 X = 0; X < Size.X; ++X)
					{
						const int32* Found = IndexOf.Find(Lo + FIntVector(X, Y, Z));
						if (!Found || Taken[*Found] || !Equal(Rep, Labels[*Found]))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		for (const int32 Start : Order)
		{
			if (Taken[Start])
			{
				continue;
			}
			const FIntVector First = Cells[Start];
			const int32 Rep = Labels[Start];
			const FIntVector MaxSpan = MaxSpanOf(Rep);
			FIntVector Span(1);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				while (Span[Axis] < MaxSpan[Axis])
				{
					FIntVector Lo = First;
					Lo[Axis] += Span[Axis];
					FIntVector Size = Span;
					Size[Axis] = 1;
					if (!BlockFree(Lo, Size, Rep))
					{
						break;
					}
					++Span[Axis];
				}
			}
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						Taken[IndexOf.FindChecked(First + FIntVector(X, Y, Z))] = true;
					}
				}
			}
			Out.Add({ First, Span, Rep });
		}
	}

	void RandomBoxes(const FPCGVoxelGrid& Grid, const FRandomPlan& Plan, int32 Seed, FEqual Equal, TArray<FBox>& Out)
	{
		FRandomStream Stream(Seed);
		TArray<bool> Taken;
		Taken.Init(false, Grid.NumCells());

		auto SlabFree = [&](const FIntVector& First, const FIntVector& Span, int32 Axis, int32 Rep)
		{
			// The layer of cells just past the box along Axis.
			FIntVector Lo = First;
			Lo[Axis] += Span[Axis];
			FIntVector Size = Span;
			Size[Axis] = 1;
			for (int32 Z = 0; Z < Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 X = 0; X < Size.X; ++X)
					{
						const int32 Cell = Grid.FindCellIndex(Lo + FIntVector(X, Y, Z));
						if (Cell == INDEX_NONE || Taken[Cell] || !Equal(Rep, Grid.CellPoint[Cell]))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		for (int32 Start = 0; Start < Grid.NumCells(); ++Start)
		{
			if (Taken[Start])
			{
				continue;
			}
			const FIntVector First = Grid.Cells[Start];
			const int32 Rep = Grid.CellPoint[Start];
			const FIntVector MaxSpan = Plan.SpanOf(Rep);

			FIntVector Span(1);
			if (Plan.Mode == EPCGVoxelRandomSize::UpTo)
			{
				// The draws are the same as before the size modes existed, so older graphs keep their layout.
				const FIntVector Target(Stream.RandRange(1, MaxSpan.X), Stream.RandRange(1, MaxSpan.Y), Stream.RandRange(1, MaxSpan.Z));
				int32 Order[3] = { 0, 1, 2 };
				for (int32 I = 2; I > 0; --I)
				{
					Swap(Order[I], Order[Stream.RandRange(0, I)]);
				}
				for (const int32 Axis : Order)
				{
					while (Span[Axis] < Target[Axis] && SlabFree(First, Span, Axis, Rep))
					{
						++Span[Axis];
					}
				}
			}
			else if (Plan.Mode == EPCGVoxelRandomSize::Exact)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					while (Span[Axis] < MaxSpan[Axis] && SlabFree(First, Span, Axis, Rep))
					{
						++Span[Axis];
					}
				}
				if (Span != MaxSpan)
				{
					Span = FIntVector(1); // the whole box does not fit: no compression here
				}
			}
			else
			{
				// A cube grows a layer on X, Y and Z together, or not at all.
				const int32 Side = Stream.RandRange(1, FMath::Min3(MaxSpan.X, MaxSpan.Y, MaxSpan.Z));
				int32 Size = 1;
				while (Size < Side)
				{
					FIntVector Trial(Size);
					bool bFits = true;
					for (int32 Axis = 0; Axis < 3 && bFits; ++Axis)
					{
						bFits = SlabFree(First, Trial, Axis, Rep);
						++Trial[Axis];
					}
					if (!bFits)
					{
						break;
					}
					++Size;
				}
				Span = FIntVector(Size);
			}

			// The chance is rolled only when there is something to compress, and only when below 1 (so a chance of 1
			// draws nothing and keeps older layouts).
			if (Span != FIntVector(1))
			{
				const double Chance = Plan.Chance.Get(Rep);
				if (Chance < 1.0 && !(Stream.FRand() < Chance))
				{
					Span = FIntVector(1);
				}
			}

			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						Taken[Grid.FindCellIndex(First + FIntVector(X, Y, Z))] = true;
					}
				}
			}
			Out.Add({ First, Span, Rep });
		}
	}
}
