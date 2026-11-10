// Copyright Max Harris

#include "Elements/PCGVoxelOffsetSurface.h"

#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Spatial/PointHashGrid3.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelOffsetSurface"

namespace PCGVoxelOffsetSurfacePrivate
{
	/** A dense box of cells, one byte each. */
	struct FMask
	{
		FIntVector Min = FIntVector::ZeroValue;
		FIntVector Dims = FIntVector::ZeroValue;
		TArray<uint8> Bits;

		int64 Index(const FIntVector& Cell) const
		{
			const FIntVector L = Cell - Min;
			return L.X + static_cast<int64>(Dims.X) * (L.Y + static_cast<int64>(Dims.Y) * L.Z);
		}
		bool Get(const FIntVector& Cell) const { return Bits[Index(Cell)] != 0; }
		FIntVector Cell(int64 I) const
		{
			const int64 X = I % Dims.X;
			const int64 Rest = I / Dims.X;
			return Min + FIntVector(static_cast<int32>(X), static_cast<int32>(Rest % Dims.Y), static_cast<int32>(Rest / Dims.Y));
		}
	};

	/**
	 * One pass of a box dilation (any set cell within Radius along Axis) or erosion (every cell within Radius set) along
	 * one axis. Cells outside the box count as empty.
	 */
	void BoxPass(FMask& Mask, int32 Axis, int32 Radius, bool bDilate)
	{
		const int32 N = Mask.Dims[Axis];
		const int64 Stride = Axis == 0 ? 1 : (Axis == 1 ? Mask.Dims.X : static_cast<int64>(Mask.Dims.X) * Mask.Dims.Y);
		const int32 A = (Axis + 1) % 3;
		const int32 B = (Axis + 2) % 3;
		const int32 NumLines = Mask.Dims[A] * Mask.Dims[B];
		TArray<uint8> Out;
		Out.SetNumZeroed(Mask.Bits.Num());
		ParallelFor(NumLines, [&](int32 Line)
		{
			FIntVector Start(0);
			Start[A] = Line % Mask.Dims[A];
			Start[B] = Line / Mask.Dims[A];
			const int64 Base = Mask.Index(Mask.Min + Start);
			TArray<int32, TInlineAllocator<256>> Prefix;
			Prefix.SetNumUninitialized(N + 1);
			Prefix[0] = 0;
			for (int32 I = 0; I < N; ++I)
			{
				Prefix[I + 1] = Prefix[I] + (Mask.Bits[Base + I * Stride] != 0);
			}
			for (int32 I = 0; I < N; ++I)
			{
				const int32 Lo = I - Radius;
				const int32 Hi = I + Radius;
				const int32 Count = Prefix[FMath::Min(Hi, N - 1) + 1] - Prefix[FMath::Max(Lo, 0)];
				const bool bSet = bDilate ? Count > 0 : (Lo >= 0 && Hi < N && Count == 2 * Radius + 1);
				Out[Base + I * Stride] = bSet ? 1 : 0;
			}
		});
		Mask.Bits = MoveTemp(Out);
	}

	/** A box inside one cell, in cell units relative to the cell centre (the whole cell is -0.5 to 0.5). */
	struct FPiece
	{
		FIntVector Cell;
		int32 Source = INDEX_NONE; // point of In this piece copies
		bool bFreshEntry = false; // Added Values None: gets a new, default metadata entry
		FVector Lo = FVector(-0.5);
		FVector Hi = FVector(0.5);
	};

	/**
	 * The part of a cell left after taking away, for each empty neighbour (offset D among the 26), every point within
	 * Fraction of it (box distance), as the fewest boxes a greedy merge finds. Appends nothing when nothing is left.
	 */
	void CarveCell(const FPiece& Whole, double Fraction, TConstArrayView<FIntVector> EmptyNeighbours, TArray<FPiece>& Out)
	{
		// Breakpoints per axis: the cell's faces and the planes Fraction in from each.
		double Cuts[3][4];
		int32 NumCuts[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			double Raw[4] = { -0.5, 0.5 - Fraction, -0.5 + Fraction, 0.5 };
			for (double& V : Raw)
			{
				V = FMath::Clamp(V, -0.5, 0.5);
			}
			Algo::Sort(Raw);
			NumCuts[Axis] = 0;
			for (const double V : Raw)
			{
				if (NumCuts[Axis] == 0 || V > Cuts[Axis][NumCuts[Axis] - 1] + 1e-9)
				{
					Cuts[Axis][NumCuts[Axis]++] = V;
				}
			}
		}
		const int32 NX = NumCuts[0] - 1, NY = NumCuts[1] - 1, NZ = NumCuts[2] - 1;
		if (NX < 1 || NY < 1 || NZ < 1)
		{
			return;
		}
		auto Mid = [&](int32 Axis, int32 I) { return 0.5 * (Cuts[Axis][I] + Cuts[Axis][I + 1]); };
		auto Removed = [&](int32 I, int32 J, int32 K)
		{
			const FVector C(Mid(0, I), Mid(1, J), Mid(2, K));
			for (const FIntVector& D : EmptyNeighbours)
			{
				bool bInside = true;
				for (int32 Axis = 0; Axis < 3 && bInside; ++Axis)
				{
					bInside = D[Axis] > 0 ? C[Axis] > 0.5 - Fraction : (D[Axis] < 0 ? C[Axis] < -0.5 + Fraction : true);
				}
				if (bInside)
				{
					return true;
				}
			}
			return false;
		};
		bool Kept[3][3][3];
		bool Used[3][3][3] = {};
		for (int32 K = 0; K < NZ; ++K)
			for (int32 J = 0; J < NY; ++J)
				for (int32 I = 0; I < NX; ++I)
					Kept[K][J][I] = !Removed(I, J, K);

		auto Free = [&](int32 I, int32 J, int32 K) { return Kept[K][J][I] && !Used[K][J][I]; };
		for (int32 K = 0; K < NZ; ++K)
			for (int32 J = 0; J < NY; ++J)
				for (int32 I = 0; I < NX; ++I)
				{
					if (!Free(I, J, K))
					{
						continue;
					}
					int32 I1 = I + 1;
					while (I1 < NX && Free(I1, J, K)) { ++I1; }
					int32 J1 = J + 1;
					auto RowFree = [&](int32 JJ, int32 KK)
					{
						for (int32 II = I; II < I1; ++II)
						{
							if (!Free(II, JJ, KK)) { return false; }
						}
						return true;
					};
					while (J1 < NY && RowFree(J1, K)) { ++J1; }
					int32 K1 = K + 1;
					auto SlabFree = [&](int32 KK)
					{
						for (int32 JJ = J; JJ < J1; ++JJ)
						{
							if (!RowFree(JJ, KK)) { return false; }
						}
						return true;
					};
					while (K1 < NZ && SlabFree(K1)) { ++K1; }
					for (int32 KK = K; KK < K1; ++KK)
						for (int32 JJ = J; JJ < J1; ++JJ)
							for (int32 II = I; II < I1; ++II)
								Used[KK][JJ][II] = true;
					FPiece& Piece = Out.Add_GetRef(Whole);
					Piece.Lo = FVector(Cuts[0][I], Cuts[1][J], Cuts[2][K]);
					Piece.Hi = FVector(Cuts[0][I1], Cuts[1][J1], Cuts[2][K1]);
				}
	}
}

#if WITH_EDITOR
FText UPCGVoxelOffsetSurfaceSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Offset Surface");
}

FText UPCGVoxelOffsetSurfaceSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Moves a voxel set's surface by Amount cells. The whole part adds or removes layers of cells; the fraction moves the "
		"open faces of each cell (its point bounds). Corners stay square, and Split Inner Corners carves inside corners on a "
		"shrink by splitting those cells into boxes. Out is In, expanded to one point per cell.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelOffsetSurfaceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets to offset. One output per input, keeping its metadata, tags and @Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelOffsetSurfaceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input offset: one point per cell (several on a split inside-corner cell), each point on its cell "
			"centre with bounds covering its box."));
	return Pins;
}

FPCGElementPtr UPCGVoxelOffsetSurfaceSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelOffsetSurfaceElement>();
}

bool FPCGVoxelOffsetSurfaceElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelOffsetSurfacePrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelOffsetSurfaceElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelOffsetSurfaceSettings* Settings = Context->GetInputSettings<UPCGVoxelOffsetSurfaceSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Offset Surface");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	const double Amount = Settings->Amount;
	if (!FMath::IsFinite(Amount))
	{
		Fail(LOCTEXT("AmountNotFinite", "Amount is not a finite number."));
		return true;
	}
	const bool bGrow = Amount > 0.0;
	const double Magnitude = FMath::Abs(Amount);
	const int32 Layers = FMath::FloorToInt32(Magnitude + 1e-9);
	double Fraction = Magnitude - Layers;
	if (Fraction < 1e-9)
	{
		Fraction = 0.0;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		if (!Points)
		{
			Fail(LOCTEXT("NotPoints", "the input is not point data; it must be a voxel set."), InputIndex);
			continue;
		}
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid))
		{
			continue;
		}
		if (Grid.NumCells() == 0)
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
			continue;
		}

		// ---- Dense box: the set's cells, padded by the grown layers plus one for neighbour checks ----
		const int32 Pad = (bGrow ? Layers : 0) + 1;
		FMask Source;
		Source.Min = Grid.MinCell - FIntVector(Pad);
		Source.Dims = Grid.MaxCell - Grid.MinCell + FIntVector(1 + 2 * Pad);
		const int64 Count = static_cast<int64>(Source.Dims.X) * Source.Dims.Y * Source.Dims.Z;
		if (Count > Settings->MaxCells)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to work over is {0} x {1} x {2} = {3} cells (the set's bounds plus {4} "
				"layers each side), over Max Cells ({5}). Raise Max Cells, or split the set."),
				Source.Dims.X, Source.Dims.Y, Source.Dims.Z, FText::AsNumber(Count), Pad, FText::AsNumber(Settings->MaxCells)), InputIndex);
			continue;
		}
		Source.Bits.SetNumZeroed(Count);
		for (const FIntVector& Cell : Grid.Cells)
		{
			Source.Bits[Source.Index(Cell)] = 1;
		}

		// ---- Whole layers: box dilation or erosion by Layers cells ----
		FMask Result = Source;
		if (Layers > 0)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				BoxPass(Result, Axis, Layers, bGrow);
			}
		}

		static const FIntVector Faces[6] = {
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
		auto IsSurface = [&](const FMask& Mask, const FIntVector& Cell)
		{
			for (const FIntVector& Step : Faces)
			{
				if (!Mask.Get(Cell + Step))
				{
					return true;
				}
			}
			return false;
		};

		// ---- Which result cells need values from the nearest cell of the old surface ----
		const bool bPush = !bGrow && Layers > 0 && Settings->RemovedCells == EPCGVoxelOffsetRemovedCells::Push;
		TArray<FIntVector> ResultCells;
		TArray<int32> Own; // the In point on the cell, or INDEX_NONE when it needs the nearest
		for (int64 I = 0; I < Count; ++I)
		{
			if (!Result.Bits[I])
			{
				continue;
			}
			const FIntVector Cell = Result.Cell(I);
			int32 Point = Source.Bits[I] ? Grid.FindPoint(Cell) : INDEX_NONE;
			if (bPush && IsSurface(Result, Cell))
			{
				Point = INDEX_NONE;
			}
			ResultCells.Add(Cell);
			Own.Add(Point);
		}

		TArray<int32> Needing;
		for (int32 Index = 0; Index < Own.Num(); ++Index)
		{
			if (Own[Index] == INDEX_NONE)
			{
				Needing.Add(Index);
			}
		}
		if (!Needing.IsEmpty())
		{
			// The nearest cell of a set to any cell outside it is on its surface, so only surface cells are searched.
			TArray<FIntVector> SurfaceCells;
			for (const FIntVector& Cell : Grid.Cells)
			{
				if (IsSurface(Source, Cell))
				{
					SurfaceCells.Add(Cell);
				}
			}
			const FVector Size = Grid.VoxelSize;
			const double HashCell = 4.0 * Size.GetMax();
			UE::Geometry::TPointHashGrid3<int32, double> Hash(HashCell, INDEX_NONE);
			for (int32 Index = 0; Index < SurfaceCells.Num(); ++Index)
			{
				Hash.InsertPointUnsafe(Index, FVector(SurfaceCells[Index]) * Size);
			}
			ParallelFor(Needing.Num(), [&](int32 N)
			{
				const FVector Query = FVector(ResultCells[Needing[N]]) * Size;
				int32 Found = INDEX_NONE;
				for (double Radius = Size.GetMax() * 1.5; Radius <= HashCell * 8.0 && Found == INDEX_NONE; Radius *= 2.0)
				{
					Found = Hash.FindNearestInRadius(Query, Radius, [&](const int32& Index)
					{
						return FVector::DistSquared(FVector(SurfaceCells[Index]) * Size, Query);
					}).Key;
				}
				if (Found == INDEX_NONE)
				{
					double Best = TNumericLimits<double>::Max();
					for (int32 Index = 0; Index < SurfaceCells.Num(); ++Index)
					{
						const double D = FVector::DistSquared(FVector(SurfaceCells[Index]) * Size, Query);
						if (D < Best)
						{
							Best = D;
							Found = Index;
						}
					}
				}
				Own[Needing[N]] = Grid.FindPoint(SurfaceCells[Found]);
			});
		}

		// ---- The fraction: per result cell, its box (or boxes) ----
		TArray<FPiece> Pieces;
		Pieces.Reserve(ResultCells.Num());
		TArray<FIntVector, TInlineAllocator<26>> Empty;
		for (int32 Index = 0; Index < ResultCells.Num(); ++Index)
		{
			const FIntVector& Cell = ResultCells[Index];
			FPiece Whole;
			Whole.Cell = Cell;
			Whole.Source = Own[Index];
			Whole.bFreshEntry = bGrow && !Source.Get(Cell) && Settings->AddedValues == EPCGVoxelOffsetAddedValues::None;
			if (Fraction == 0.0)
			{
				Pieces.Add(Whole);
				continue;
			}
			const double Signed = bGrow ? Fraction : -Fraction;
			bool bAnyEmpty = false;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FIntVector Step(0);
				Step[Axis] = 1;
				if (!Result.Get(Cell + Step)) { Whole.Hi[Axis] += Signed; bAnyEmpty = true; }
				if (!Result.Get(Cell - Step)) { Whole.Lo[Axis] -= Signed; bAnyEmpty = true; }
			}
			if (bGrow || !Settings->bSplitInnerCorners)
			{
				if ((Whole.Hi - Whole.Lo).GetMin() > 1e-6)
				{
					Pieces.Add(Whole);
				}
				continue;
			}
			// Shrinking with inside corners: carve the parts within Fraction of every empty neighbour.
			Empty.Reset();
			for (int32 Z = -1; Z <= 1; ++Z)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 X = -1; X <= 1; ++X)
					{
						const FIntVector D(X, Y, Z);
						if (D != FIntVector::ZeroValue && !Result.Get(Cell + D))
						{
							Empty.Add(D);
						}
					}
			if (Empty.IsEmpty())
			{
				Pieces.Add(Whole);
				continue;
			}
			FPiece Cube = Whole;
			Cube.Lo = FVector(-0.5);
			Cube.Hi = FVector(0.5);
			CarveCell(Cube, Fraction, Empty, Pieces);
		}

		// ---- Write ----
		TArray<FPCGVoxelCellOut> Outs;
		Outs.Reserve(Pieces.Num());
		for (const FPiece& Piece : Pieces)
		{
			Outs.Add({ Piece.Source, Piece.Cell, FIntVector(1) });
		}
		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Outs, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false);

		const FQuat GridRotation = Grid.GridToWorld.GetRotation();
		TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
		TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
		TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
		TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
		FPCGMetadataDomain* OutDomain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		{
			const FPiece& Piece = Pieces[Index];
			// The box in grid axes, relative to the cell centre (where WriteCells put the point), into the point's frame.
			const FQuat ToPoint = Transforms[Index].GetRotation().Inverse() * GridRotation;
			const FVector Lo = Piece.Lo * Grid.VoxelSize;
			const FVector Hi = Piece.Hi * Grid.VoxelSize;
			FBox Local(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				Local += ToPoint.RotateVector(FVector((Corner & 1) ? Hi.X : Lo.X, (Corner & 2) ? Hi.Y : Lo.Y, (Corner & 4) ? Hi.Z : Lo.Z));
			}
			FVector Scale = Transforms[Index].GetScale3D().GetAbs();
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (Scale[Axis] < UE_SMALL_NUMBER)
				{
					Scale[Axis] = 1.0;
				}
			}
			Mins[Index] = Local.Min / Scale;
			Maxs[Index] = Local.Max / Scale;
			if (Piece.bFreshEntry && OutDomain)
			{
				Entries[Index] = OutDomain->AddEntry();
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
