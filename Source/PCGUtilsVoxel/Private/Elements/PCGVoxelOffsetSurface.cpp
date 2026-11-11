// Copyright Max Harris

#include "Elements/PCGVoxelOffsetSurface.h"

#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "Details/PCGUtilsInputShorthands.h"
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

	/** Counts of empty cells over boxes in O(1): a summed-volume table over a mask's box. Outside the box is empty. */
	struct FEmptyTable
	{
		FIntVector Dims = FIntVector::ZeroValue; // the mask's dims; the table is one larger on each axis
		TArray<int32> Sum;

		int64 At(int32 X, int32 Y, int32 Z) const
		{
			return X + static_cast<int64>(Dims.X + 1) * (Y + static_cast<int64>(Dims.Y + 1) * Z);
		}

		void Build(const FMask& Mask)
		{
			Dims = Mask.Dims;
			Sum.SetNumZeroed(static_cast<int64>(Dims.X + 1) * (Dims.Y + 1) * (Dims.Z + 1));
			for (int32 Z = 0; Z < Dims.Z; ++Z)
				for (int32 Y = 0; Y < Dims.Y; ++Y)
					for (int32 X = 0; X < Dims.X; ++X)
					{
						const int32 Empty = Mask.Bits[X + static_cast<int64>(Dims.X) * (Y + static_cast<int64>(Dims.Y) * Z)] ? 0 : 1;
						Sum[At(X + 1, Y + 1, Z + 1)] = Empty
							+ Sum[At(X, Y + 1, Z + 1)] + Sum[At(X + 1, Y, Z + 1)] + Sum[At(X + 1, Y + 1, Z)]
							- Sum[At(X, Y, Z + 1)] - Sum[At(X, Y + 1, Z)] - Sum[At(X + 1, Y, Z)]
							+ Sum[At(X, Y, Z)];
					}
		}

		/** True when any cell within Reach of Local (box, per axis) is empty. */
		bool AnyEmpty(const FIntVector& Local, const FIntVector& Reach) const
		{
			const FIntVector Lo = Local - Reach;
			const FIntVector Hi = Local + Reach + FIntVector(1);
			if (Lo.X < 0 || Lo.Y < 0 || Lo.Z < 0 || Hi.X > Dims.X || Hi.Y > Dims.Y || Hi.Z > Dims.Z)
			{
				return true;
			}
			const int64 Count = static_cast<int64>(Sum[At(Hi.X, Hi.Y, Hi.Z)])
				- Sum[At(Lo.X, Hi.Y, Hi.Z)] - Sum[At(Hi.X, Lo.Y, Hi.Z)] - Sum[At(Hi.X, Hi.Y, Lo.Z)]
				+ Sum[At(Lo.X, Lo.Y, Hi.Z)] + Sum[At(Lo.X, Hi.Y, Lo.Z)] + Sum[At(Hi.X, Lo.Y, Lo.Z)]
				- Sum[At(Lo.X, Lo.Y, Lo.Z)];
			return Count > 0;
		}
	};

	/** One input cell's offset: its sign, the reach in whole cells per axis and the fraction left over per axis. */
	struct FCellOffset
	{
		int32 Sign = 0;
		FIntVector Layers = FIntVector::ZeroValue;
		FVector Fraction = FVector::ZeroVector;
		FVector Magnitude = FVector::ZeroVector; // in cells per axis
	};

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
	 * Fraction of it (per axis, box distance), as the fewest boxes a greedy merge finds. Appends nothing when nothing is left.
	 */
	void CarveCell(const FPiece& Whole, const FVector& Fraction, TConstArrayView<FIntVector> EmptyNeighbours, TArray<FPiece>& Out)
	{
		// Breakpoints per axis: the cell's faces and the planes Fraction in from each.
		double Cuts[3][4];
		int32 NumCuts[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double F = Fraction[Axis];
			double Raw[4] = { -0.5, 0.5 - F, -0.5 + F, 0.5 };
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
					bInside = D[Axis] > 0 ? C[Axis] > 0.5 - Fraction[Axis] : (D[Axis] < 0 ? C[Axis] < -0.5 + Fraction[Axis] : true);
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
					auto RowFree = [&](int32 JJ, int32 KK)
					{
						for (int32 II = I; II < I1; ++II)
						{
							if (!Free(II, JJ, KK)) { return false; }
						}
						return true;
					};
					int32 J1 = J + 1;
					while (J1 < NY && RowFree(J1, K)) { ++J1; }
					auto SlabFree = [&](int32 KK)
					{
						for (int32 JJ = J; JJ < J1; ++JJ)
						{
							if (!RowFree(JJ, KK)) { return false; }
						}
						return true;
					};
					int32 K1 = K + 1;
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

UPCGVoxelOffsetSurfaceSettings::UPCGVoxelOffsetSurfaceSettings()
{
	AmountScaleAttribute.SetAttributeName(TEXT("AmountScale"));
}

#if WITH_EDITOR
FText UPCGVoxelOffsetSurfaceSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Offset Surface");
}

FText UPCGVoxelOffsetSurfaceSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Moves a voxel set's surface by Amount x Amount Scale (cells or world units; the scale can be per cell from a point "
		"attribute). The whole part adds or removes layers of cells; the fraction moves the open faces of each cell (its "
		"point bounds). Corners stay square, and Split Inner Corners carves inside corners on a shrink by splitting those "
		"cells into boxes. Out is In, expanded to one point per cell.");
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

	if (!FMath::IsFinite(Settings->Amount))
	{
		Fail(LOCTEXT("AmountNotFinite", "Amount is not a finite number."));
		return true;
	}
	const bool bAttributeScale = Settings->AmountScaleInput == EPCGVoxelOffsetScaleInput::Attribute;
	if (!bAttributeScale && !FMath::IsFinite(Settings->AmountScale))
	{
		Fail(LOCTEXT("ScaleNotFinite", "Amount Scale is not a finite number."));
		return true;
	}

	static const FIntVector Faces[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	auto IsSurface = [](const FMask& Mask, const FIntVector& Cell)
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

		// ---- Amount Scale: constant, one @Data value for the set, or one value per point ----
		TArray<double> PointScale;
		double SetScale = Settings->AmountScale;
		if (bAttributeScale)
		{
			const FPCGAttributePropertyInputSelector& Selector = Settings->AmountScaleAttribute;
			FPCGMetadataDomainID Domain = Points->GetMetadataDomainIDFromSelector(Selector);
			if (Domain == PCGMetadataDomainID::Default)
			{
				Domain = Points->GetDefaultMetadataDomainID();
			}
			if (Domain == PCGMetadataDomainID::Data)
			{
				FText Reason;
				if (!PCGUtilsInputShorthands::ReadDataDomainValue(Points, Selector, SetScale, Reason))
				{
					Fail(FText::Format(LOCTEXT("ScaleDataRead", "Amount Scale Attribute: {0}."), Reason), InputIndex);
					continue;
				}
				if (!FMath::IsFinite(SetScale))
				{
					Fail(FText::Format(LOCTEXT("ScaleDataNotFinite", "Amount Scale Attribute '{0}' is not a finite number."), Selector.GetDisplayText()), InputIndex);
					continue;
				}
			}
			else if (!PCGVoxelPointIO::ReadPointValues<double>(Context, Points, Selector, PointScale, Title, InputIndex,
				LOCTEXT("ScaleSetting", "Amount Scale Attribute"), LOCTEXT("ScaleType", "a number")))
			{
				continue;
			}
		}

		// ---- Each cell's offset, in cells per axis ----
		const FVector UnitToCells = Settings->Units == EPCGVoxelOffsetUnits::Cells ? FVector(1.0) : FVector(1.0) / Grid.VoxelSize;
		const FVector ReserveWeight = Settings->Units == EPCGVoxelOffsetUnits::Cells ? FVector(1.0) : Grid.VoxelSize;
		TArray<FCellOffset> Offsets;
		Offsets.SetNum(Grid.NumCells());
		FIntVector GrowReach = FIntVector::ZeroValue;
		bool bAnyLayers = false;
		bool bBadScale = false;
		for (int32 Index = 0; Index < Grid.NumCells() && !bBadScale; ++Index)
		{
			const int32 Point = Grid.CellPoint[Index];
			const double Scale = PointScale.IsEmpty() ? SetScale : PointScale[Point];
			const double Value = Settings->Amount * Scale;
			if (!FMath::IsFinite(Value))
			{
				Fail(FText::Format(LOCTEXT("ScalePointNotFinite", "Amount Scale Attribute on point {0} is {1}; Amount x Amount Scale must be a finite number."),
					Point, FText::AsNumber(Scale)), InputIndex);
				bBadScale = true;
				break;
			}
			FCellOffset& Offset = Offsets[Index];
			Offset.Sign = Value > 0.0 ? 1 : (Value < 0.0 ? -1 : 0);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				// Capped so the cast cannot overflow; Max Cells stops anything near the cap.
				const double M = FMath::Min(FMath::Abs(Value) * UnitToCells[Axis], 1.0e6);
				const int32 L = FMath::FloorToInt32(M + 1e-9);
				Offset.Magnitude[Axis] = M;
				Offset.Layers[Axis] = L;
				Offset.Fraction[Axis] = M - L < 1e-9 ? 0.0 : M - L;
			}
			if (Offset.Layers != FIntVector::ZeroValue && Offset.Sign != 0)
			{
				bAnyLayers = true;
				if (Offset.Sign > 0)
				{
					GrowReach = FIntVector(FMath::Max(GrowReach.X, Offset.Layers.X), FMath::Max(GrowReach.Y, Offset.Layers.Y), FMath::Max(GrowReach.Z, Offset.Layers.Z));
				}
			}
		}
		if (bBadScale)
		{
			continue;
		}

		// ---- Dense box: the set's cells, padded by the grown layers plus one for neighbour checks ----
		const FIntVector Pad = GrowReach + FIntVector(1);
		FMask Source;
		Source.Min = Grid.MinCell - Pad;
		const FIntVector Dims = Grid.MaxCell - Grid.MinCell + FIntVector(1) + Pad * 2;
		const int64 Count = static_cast<int64>(Dims.X) * Dims.Y * Dims.Z;
		if (Count > Settings->MaxCells)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to work over is {0} x {1} x {2} = {3} cells (the set's bounds plus the grown "
				"layers and one cell each side), over Max Cells ({4}). Raise Max Cells, or split the set."),
				Dims.X, Dims.Y, Dims.Z, FText::AsNumber(Count), FText::AsNumber(Settings->MaxCells)), InputIndex);
			continue;
		}
		if (Count >= MAX_int32)
		{
			Fail(FText::Format(LOCTEXT("TooBigHard", "the box of cells to work over is {0} cells; this node handles under 2^31."), FText::AsNumber(Count)), InputIndex);
			continue;
		}
		Source.Dims = Dims;
		Source.Bits.SetNumZeroed(Count);
		for (const FIntVector& Cell : Grid.Cells)
		{
			Source.Bits[Source.Index(Cell)] = 1;
		}

		// ---- Whole layers: each cell reaches Layers cells (a box) ----
		// A shrinking cell goes when an empty cell is within its reach; a growing cell fills every empty cell within its
		// reach, and an empty cell reached by several takes the one with the most reach to spare (then the closest).
		FMask Result = Source;
		TArray<int32> Origin; // per dense cell: the input cell that grew into it
		if (bAnyLayers)
		{
			FEmptyTable Empty;
			Empty.Build(Source);
			ParallelFor(Grid.NumCells(), [&](int32 Index)
			{
				const FCellOffset& Offset = Offsets[Index];
				if (Offset.Sign < 0 && Offset.Layers != FIntVector::ZeroValue && Empty.AnyEmpty(Grid.Cells[Index] - Source.Min, Offset.Layers))
				{
					Result.Bits[Source.Index(Grid.Cells[Index])] = 0;
				}
			});

			TArray<float> Reserve;
			TArray<float> Distance;
			for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
			{
				const FCellOffset& Offset = Offsets[Index];
				const FIntVector From = Grid.Cells[Index];
				if (Offset.Sign <= 0 || Offset.Layers == FIntVector::ZeroValue || !Empty.AnyEmpty(From - Source.Min, Offset.Layers))
				{
					continue;
				}
				if (Origin.IsEmpty())
				{
					Origin.Init(INDEX_NONE, Count);
					Reserve.Init(-1.0f, Count);
					Distance.Init(0.0f, Count);
				}
				const FIntVector L = Offset.Layers;
				for (int32 DZ = -L.Z; DZ <= L.Z; ++DZ)
					for (int32 DY = -L.Y; DY <= L.Y; ++DY)
						for (int32 DX = -L.X; DX <= L.X; ++DX)
						{
							const FIntVector Cell = From + FIntVector(DX, DY, DZ);
							const int64 I = Source.Index(Cell);
							if (Source.Bits[I])
							{
								continue;
							}
							const float Spare = static_cast<float>(FMath::Min3(
								(Offset.Magnitude.X - FMath::Abs(DX)) * ReserveWeight.X,
								(Offset.Magnitude.Y - FMath::Abs(DY)) * ReserveWeight.Y,
								(Offset.Magnitude.Z - FMath::Abs(DZ)) * ReserveWeight.Z));
							const float Dist = static_cast<float>((FVector(DX, DY, DZ) * Grid.VoxelSize).SizeSquared());
							const bool bBetter = Origin[I] == INDEX_NONE || Spare > Reserve[I] + 1e-4f
								|| (Spare > Reserve[I] - 1e-4f && Dist < Distance[I] - 1e-3f);
							if (bBetter)
							{
								Origin[I] = Index;
								Reserve[I] = Spare;
								Distance[I] = Dist;
								Result.Bits[I] = 1;
							}
						}
			}
		}

		// ---- Result cells: whose values and whose offset each takes ----
		const bool bPush = Settings->RemovedCells == EPCGVoxelOffsetRemovedCells::Push;
		TArray<FIntVector> ResultCells;
		TArray<int32> OffsetOf; // index into Grid.Cells / Offsets
		TArray<int32> Own; // the In point copied, or INDEX_NONE when it needs the nearest old surface cell (Push)
		TArray<bool> Added;
		for (int64 I = 0; I < Count; ++I)
		{
			if (!Result.Bits[I])
			{
				continue;
			}
			const FIntVector Cell = Result.Cell(I);
			if (Source.Bits[I])
			{
				const int32 Index = Grid.FindCellIndex(Cell);
				const bool bExposed = bPush && IsSurface(Result, Cell) && !IsSurface(Source, Cell);
				ResultCells.Add(Cell);
				OffsetOf.Add(Index);
				Own.Add(bExposed ? INDEX_NONE : Grid.CellPoint[Index]);
				Added.Add(false);
			}
			else
			{
				const int32 From = Origin[I];
				ResultCells.Add(Cell);
				OffsetOf.Add(From);
				Own.Add(Grid.CellPoint[From]);
				Added.Add(true);
			}
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
			// Push: the nearest cell of the old surface, by centre distance.
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
		TArray<FIntVector, TInlineAllocator<26>> EmptyNeighbours;
		for (int32 Index = 0; Index < ResultCells.Num(); ++Index)
		{
			const FIntVector& Cell = ResultCells[Index];
			const FCellOffset& Offset = Offsets[OffsetOf[Index]];
			FPiece Whole;
			Whole.Cell = Cell;
			Whole.Source = Own[Index];
			Whole.bFreshEntry = Added[Index] && Settings->AddedValues == EPCGVoxelOffsetAddedValues::None;
			if (Offset.Sign == 0 || Offset.Fraction.IsZero())
			{
				Pieces.Add(Whole);
				continue;
			}
			const bool bGrow = Offset.Sign > 0;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const double F = bGrow ? Offset.Fraction[Axis] : -Offset.Fraction[Axis];
				FIntVector Step(0);
				Step[Axis] = 1;
				if (!Result.Get(Cell + Step)) { Whole.Hi[Axis] += F; }
				if (!Result.Get(Cell - Step)) { Whole.Lo[Axis] -= F; }
			}
			if (bGrow || !Settings->bSplitInnerCorners)
			{
				if ((Whole.Hi - Whole.Lo).GetMin() > 1e-6)
				{
					Pieces.Add(Whole);
				}
				continue;
			}
			// Shrinking with inside corners: carve the parts within the fraction of every empty neighbour.
			EmptyNeighbours.Reset();
			for (int32 Z = -1; Z <= 1; ++Z)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 X = -1; X <= 1; ++X)
					{
						const FIntVector D(X, Y, Z);
						if (D != FIntVector::ZeroValue && !Result.Get(Cell + D))
						{
							EmptyNeighbours.Add(D);
						}
					}
			FPiece Cube = Whole;
			Cube.Lo = FVector(-0.5);
			Cube.Hi = FVector(0.5);
			if (EmptyNeighbours.IsEmpty())
			{
				Pieces.Add(Cube);
				continue;
			}
			CarveCell(Cube, Offset.Fraction, EmptyNeighbours, Pieces);
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
