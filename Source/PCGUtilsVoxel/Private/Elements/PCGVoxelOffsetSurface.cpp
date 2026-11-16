// Copyright Max Harris

#include "Elements/PCGVoxelOffsetSurface.h"

#include "Algo/Sort.h"
#include "Algo/StableSort.h"
#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Spatial/PointHashGrid3.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelMorphology.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelOffsetSurface"

namespace PCGVoxelOffsetSurfacePrivate
{
	/**
	 * One input cell's offset, per direction ([0] towards -axis, [1] towards +axis). Each axis grows or shrinks by its
	 * own sign (Amount x Amount Scale on that axis); a direction left out of Directions does neither.
	 */
	struct FCellOffset
	{
		FVector GrowMagnitude[2] = { FVector::ZeroVector, FVector::ZeroVector }; // in cells
		FIntVector GrowReach[2] = { FIntVector::ZeroValue, FIntVector::ZeroValue }; // cells its grown box touches (whole layers, plus one with a fraction)
		FIntVector ShrinkLayers[2] = { FIntVector::ZeroValue, FIntVector::ZeroValue }; // whole layers removed
		FVector ShrinkFraction[2] = { FVector::ZeroVector, FVector::ZeroVector }; // how far the open face then moves in

		bool Grows() const { return GrowReach[0] != FIntVector::ZeroValue || GrowReach[1] != FIntVector::ZeroValue; }
		bool RemovesLayers() const { return ShrinkLayers[0] != FIntVector::ZeroValue || ShrinkLayers[1] != FIntVector::ZeroValue; }
		bool Shrinks() const { return RemovesLayers() || !ShrinkFraction[0].IsZero() || !ShrinkFraction[1].IsZero(); }
	};

	/** A shrinking cell's face fractions: Lo towards -axis, Hi towards +axis. */
	struct FShrink
	{
		FVector Lo = FVector::ZeroVector;
		FVector Hi = FVector::ZeroVector;
		bool IsZero() const { return Lo.IsZero() && Hi.IsZero(); }
	};

	/** A growing cell's reach clipped to an empty cell it covers only in part. */
	struct FPartialHit
	{
		int64 I = 0; // dense index of the covered cell
		int32 From = INDEX_NONE; // index into Grid.Cells of the growing cell
		FVector Lo = FVector(-0.5);
		FVector Hi = FVector(0.5);
		float Spare = 0.0f;
		float Dist = 0.0f;
	};

	/** Sorted, de-duplicated cut planes on one axis, always including the cell faces. */
	void FinishCuts(TArray<double>& Cuts)
	{
		for (double& V : Cuts)
		{
			V = FMath::Clamp(V, -0.5, 0.5);
		}
		Cuts.Add(-0.5);
		Cuts.Add(0.5);
		Algo::Sort(Cuts);
		int32 Kept = 0;
		for (const double V : Cuts)
		{
			if (Kept == 0 || V > Cuts[Kept - 1] + 1e-9)
			{
				Cuts[Kept++] = V;
			}
		}
		Cuts.SetNum(Kept);
	}

	/**
	 * Greedy merge of a grid of sub-boxes (between the cut planes), each labelled (INDEX_NONE = empty), into boxes of one
	 * label each: grows along X, then Y, then Z. Calls Emit(Label, Lo, Hi) per box.
	 */
	template <typename EmitType>
	void MergeLabels(const TArray<double> (&Cuts)[3], TConstArrayView<int32> Labels, EmitType&& Emit)
	{
		const int32 NX = Cuts[0].Num() - 1, NY = Cuts[1].Num() - 1, NZ = Cuts[2].Num() - 1;
		if (NX < 1 || NY < 1 || NZ < 1)
		{
			return;
		}
		auto At = [&](int32 I, int32 J, int32 K) { return I + NX * (J + NY * K); };
		TArray<bool> Used;
		Used.SetNumZeroed(NX * NY * NZ);
		for (int32 K = 0; K < NZ; ++K)
			for (int32 J = 0; J < NY; ++J)
				for (int32 I = 0; I < NX; ++I)
				{
					const int32 Label = Labels[At(I, J, K)];
					if (Label == INDEX_NONE || Used[At(I, J, K)])
					{
						continue;
					}
					auto Free = [&](int32 II, int32 JJ, int32 KK) { return Labels[At(II, JJ, KK)] == Label && !Used[At(II, JJ, KK)]; };
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
								Used[At(II, JJ, KK)] = true;
					Emit(Label, FVector(Cuts[0][I], Cuts[1][J], Cuts[2][K]), FVector(Cuts[0][I1], Cuts[1][J1], Cuts[2][K1]));
				}
	}

	/**
	 * The union of the parts of one empty cell that growing cells reach, as boxes that do not overlap. Each part of the
	 * cell goes to the hit with the most reach to spare there (then the closest, then the first), and boxes are merged
	 * only within one hit, so each box copies one source. Calls Emit(HitIndex, Lo, Hi) per box.
	 */
	template <typename EmitType>
	void UnionHits(TConstArrayView<FPartialHit> Hits, EmitType&& Emit)
	{
		TArray<double> Cuts[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (const FPartialHit& Hit : Hits)
			{
				Cuts[Axis].Add(Hit.Lo[Axis]);
				Cuts[Axis].Add(Hit.Hi[Axis]);
			}
			FinishCuts(Cuts[Axis]);
		}
		const int32 NX = Cuts[0].Num() - 1, NY = Cuts[1].Num() - 1, NZ = Cuts[2].Num() - 1;
		TArray<int32> Labels;
		Labels.Init(INDEX_NONE, NX * NY * NZ);
		for (int32 K = 0; K < NZ; ++K)
			for (int32 J = 0; J < NY; ++J)
				for (int32 I = 0; I < NX; ++I)
				{
					const FVector Mid(0.5 * (Cuts[0][I] + Cuts[0][I + 1]), 0.5 * (Cuts[1][J] + Cuts[1][J + 1]), 0.5 * (Cuts[2][K] + Cuts[2][K + 1]));
					int32 Best = INDEX_NONE;
					for (int32 H = 0; H < Hits.Num(); ++H)
					{
						const FPartialHit& Hit = Hits[H];
						if (Mid.X <= Hit.Lo.X || Mid.X >= Hit.Hi.X || Mid.Y <= Hit.Lo.Y || Mid.Y >= Hit.Hi.Y || Mid.Z <= Hit.Lo.Z || Mid.Z >= Hit.Hi.Z)
						{
							continue;
						}
						if (Best == INDEX_NONE || Hit.Spare > Hits[Best].Spare + 1e-4f
							|| (Hit.Spare > Hits[Best].Spare - 1e-4f && Hit.Dist < Hits[Best].Dist - 1e-3f))
						{
							Best = H;
						}
					}
					Labels[I + NX * (J + NY * K)] = Best;
				}
		MergeLabels(Cuts, Labels, Emit);
	}


	/** A box inside one cell, in cell units relative to the cell centre (the whole cell is -0.5 to 0.5). */
	struct FPiece
	{
		FIntVector Cell;
		int32 Source = INDEX_NONE; // point of In this piece copies
		bool bFreshEntry = false; // Added Values None: gets a new, default metadata entry
		bool bAdded = false; // a whole cell the offset added
		FIntVector Span = FIntVector(1); // a chunk of cells (Compression); Lo and Hi are then relative to its centre
		FVector Lo = FVector(-0.5);
		FVector Hi = FVector(0.5);
	};

	/**
	 * The part of a cell left after taking away, for each empty neighbour (offset D among the 26), every point within
	 * Fraction of it (per axis, box distance), as the fewest boxes a greedy merge finds. Appends nothing when nothing is left.
	 */
	void CarveCell(const FPiece& Whole, const FShrink& Fraction, TConstArrayView<FIntVector> EmptyNeighbours, TArray<FPiece>& Out)
	{
		// Breakpoints per axis: the cell's faces and the planes each face's fraction in from it.
		TArray<double> Cuts[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Cuts[Axis] = { 0.5 - Fraction.Hi[Axis], -0.5 + Fraction.Lo[Axis] };
			FinishCuts(Cuts[Axis]);
		}
		const int32 NX = Cuts[0].Num() - 1, NY = Cuts[1].Num() - 1, NZ = Cuts[2].Num() - 1;
		if (NX < 1 || NY < 1 || NZ < 1)
		{
			return;
		}
		TArray<int32> Labels;
		Labels.SetNumUninitialized(NX * NY * NZ);
		for (int32 K = 0; K < NZ; ++K)
			for (int32 J = 0; J < NY; ++J)
				for (int32 I = 0; I < NX; ++I)
				{
					const FVector C(0.5 * (Cuts[0][I] + Cuts[0][I + 1]), 0.5 * (Cuts[1][J] + Cuts[1][J + 1]), 0.5 * (Cuts[2][K] + Cuts[2][K + 1]));
					bool bRemoved = false;
					for (const FIntVector& D : EmptyNeighbours)
					{
						bool bInside = true;
						for (int32 Axis = 0; Axis < 3 && bInside; ++Axis)
						{
							bInside = D[Axis] > 0 ? C[Axis] > 0.5 - Fraction.Hi[Axis] : (D[Axis] < 0 ? C[Axis] < -0.5 + Fraction.Lo[Axis] : true);
						}
						if (bInside)
						{
							bRemoved = true;
							break;
						}
					}
					Labels[I + NX * (J + NY * K)] = bRemoved ? INDEX_NONE : 0;
				}
		MergeLabels(Cuts, Labels, [&](int32, const FVector& Lo, const FVector& Hi)
		{
			FPiece& Piece = Out.Add_GetRef(Whole);
			Piece.Lo = Lo;
			Piece.Hi = Hi;
		});
	}
}

UPCGVoxelOffsetSurfaceSettings::UPCGVoxelOffsetSurfaceSettings()
{
	AmountScaleAttribute_DEPRECATED.SetAttributeName(TEXT("AmountScale"));
}

void UPCGVoxelOffsetSurfaceSettings::PostLoad()
{
	Super::PostLoad();
	// Axes (X, Y, Z) became Directions (each sign on its own): an older graph's axes keep both their directions.
	if (Axes != static_cast<int32>(EPCGVoxelAxes::All))
	{
		Directions = PCGVoxel::DirectionsFromAxes(Axes);
		Axes = static_cast<int32>(EPCGVoxelAxes::All);
	}
	// Amount Scale was a number plus an Input enum and an attribute; the number loads into the shorthand's constant
	// by itself, the attribute mode is carried over here.
	if (AmountScaleInput_DEPRECATED == EPCGVoxelOffsetScaleInput::Attribute)
	{
		AmountScale.Input = EPCGUtilsPointInputValueType::Attribute;
		AmountScale.Attribute = AmountScaleAttribute_DEPRECATED;
		AmountScaleInput_DEPRECATED = EPCGVoxelOffsetScaleInput::Constant;
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
		"Moves a voxel set's surface by Amount x Amount Scale (cells or world units; each a constant, a per-cell point "
		"attribute or an @Data value). The whole part adds or removes layers of cells; the fraction is a part cell (its point bounds): "
		"growing 3.5 adds three cells and then cells half deep. No box ever leaves its cell. Corners stay square; an inside corner "
		"grown, or carved on a shrink with Split Inner Corners, is several boxes on one cell (flagged). Axes limits the offset to "
		"some grid axes (X | Y leaves Z alone).");
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
		LOCTEXT("OutTooltip", "Each input offset, still a voxel set: one point per box, each box inside its own cell (point bounds). "
			"A cell that takes more than one box (a grown or carved inside corner) has several points, flagged with Was Split "
			"Attribute. Is Surrounded is rewritten; filter on it to spawn only visible boxes."));
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

	if (Settings->Amount.IsConstant() && !FMath::IsFinite(Settings->Amount.Constant))
	{
		Fail(LOCTEXT("AmountNotFinite", "Amount is not a finite number."));
		return true;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Settings->AmountScale.IsConstant() && !FMath::IsFinite(Settings->AmountScale.Constant[Axis]))
		{
			Fail(FText::Format(LOCTEXT("ScaleNotFinite", "Amount Scale {0} is not a finite number."), FText::FromString(Axis == 0 ? TEXT("X") : (Axis == 1 ? TEXT("Y") : TEXT("Z")))));
			return true;
		}
	}

	static const FIntVector Faces[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	auto IsSurface = [](const FPCGVoxelDenseMask& Mask, const FIntVector& Cell)
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
			Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]).Pin = PCGPinConstants::DefaultOutputLabel;
			continue;
		}

		// ---- Amount and Amount Scale: a constant, one @Data value for the set, or one value per point ----
		TPCGUtilsPointValues<double> AmountValues;
		TPCGUtilsPointValues<FVector> ScaleValues;
		FText ReadError;
		if (!Settings->Amount.TryRead(Points, AmountValues, ReadError, LOCTEXT("AmountLabel", "Amount")))
		{
			Fail(ReadError, InputIndex);
			continue;
		}
		if (!Settings->AmountScale.TryRead(Points, ScaleValues, ReadError, LOCTEXT("ScaleLabel", "Amount Scale")))
		{
			Fail(ReadError, InputIndex);
			continue;
		}
		FPCGVoxelPointEquality ChunkEqual;
		if (Settings->bCompressEqualCells)
		{
			FName Missing;
			if (!ChunkEqual.Init(Points, Settings->MatchAttributes, Settings->ListedAttributes, Settings->Grid.SpanAttribute, Settings->Grid.SurroundedAttribute, Missing))
			{
				Fail(FText::Format(LOCTEXT("ListedMissing", "Listed Attributes names '{0}', which is not a point attribute of this input."), FText::FromName(Missing)), InputIndex);
				continue;
			}
		}
		TPCGUtilsPointValues<double> MinThicknessValues;
		if (Settings->bLimitThinness
			&& !Settings->MinThickness.TryRead(Points, MinThicknessValues, ReadError, LOCTEXT("MinThicknessLabel", "Min Thickness")))
		{
			Fail(ReadError, InputIndex);
			continue;
		}

		// ---- Each cell's offset, in cells per axis ----
		const FVector UnitToCells = Settings->Units == EPCGVoxelOffsetUnits::Cells ? FVector(1.0) : FVector(1.0) / Grid.VoxelSize;
		const FVector ReserveWeight = Settings->Units == EPCGVoxelOffsetUnits::Cells ? FVector(1.0) : Grid.VoxelSize;
		// Directions: a direction left out gets no offset at all (no layers, no face movement, no carving that way).
		bool DirOn[2][3];
		bool AxisOn[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			DirOn[0][Axis] = (Settings->Directions & PCGVoxel::DirectionBit(Axis, -1)) != 0;
			DirOn[1][Axis] = (Settings->Directions & PCGVoxel::DirectionBit(Axis, 1)) != 0;
			AxisOn[Axis] = DirOn[0][Axis] || DirOn[1][Axis];
		}
		TArray<FCellOffset> Offsets;
		Offsets.SetNum(Grid.NumCells());
		FIntVector GrowReach = FIntVector::ZeroValue;
		bool bAnyLayers = false;
		bool bBadScale = false;
		for (int32 Index = 0; Index < Grid.NumCells() && !bBadScale; ++Index)
		{
			const int32 Point = Grid.CellPoint[Index];
			const FVector Scale = ScaleValues.Get(Point);
			FCellOffset& Offset = Offsets[Index];
			for (int32 Axis = 0; Axis < 3 && !bBadScale; ++Axis)
			{
				const double Value = AmountValues.Get(Point) * Scale[Axis];
				if (!FMath::IsFinite(Value))
				{
					Fail(FText::Format(LOCTEXT("ProductNotFinite", "on point {0}, Amount ({1}) x Amount Scale {2} ({3}) overflows; the product must be a finite number."),
						Point, FText::AsNumber(AmountValues.Get(Point)), FText::FromString(Axis == 0 ? TEXT("X") : (Axis == 1 ? TEXT("Y") : TEXT("Z"))),
						FText::AsNumber(Scale[Axis])), InputIndex);
					bBadScale = true;
					break;
				}
				for (int32 Side = 0; Side < 2; ++Side)
				{
					if (!DirOn[Side][Axis] || Value == 0.0)
					{
						continue;
					}
					// Capped so the cast cannot overflow; Max Cells stops anything near the cap.
					const double M = FMath::Min(FMath::Abs(Value) * UnitToCells[Axis], 1.0e6);
					const int32 L = FMath::FloorToInt32(M + 1e-9);
					const double F = M - L < 1e-9 ? 0.0 : M - L;
					if (Value > 0.0)
					{
						Offset.GrowMagnitude[Side][Axis] = M;
						Offset.GrowReach[Side][Axis] = L + (F > 0.0 ? 1 : 0);
					}
					else
					{
						Offset.ShrinkLayers[Side][Axis] = L;
						Offset.ShrinkFraction[Side][Axis] = F;
					}
				}
			}
			if (Offset.RemovesLayers())
			{
				bAnyLayers = true;
			}
			if (Offset.Grows())
			{
				bAnyLayers = true;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					GrowReach = FIntVector(FMath::Max(GrowReach.X, Offset.GrowReach[Side].X), FMath::Max(GrowReach.Y, Offset.GrowReach[Side].Y),
						FMath::Max(GrowReach.Z, Offset.GrowReach[Side].Z));
				}
			}
		}
		if (bBadScale)
		{
			continue;
		}

		// ---- Dense box: the set's cells, padded by the growing reach plus one for neighbour checks ----
		const FIntVector Pad = GrowReach + FIntVector(1);
		FPCGVoxelDenseMask Source;
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

		// ---- Whole layers and growing ----
		// A shrinking cell goes when an empty cell is within its whole layers (a box). A growing cell reaches its own box
		// grown by its amount on each axis; that reach, clipped to each empty cell it touches, is what it gives that cell,
		// so no box ever leaves its cell. A cell covered whole is filled, taking the grower with the most reach to spare
		// (then the closest); a cell covered only in part keeps the union of the parts reaching it, each part copying its
		// own grower (an inside corner gets an L from its two walls). Result marks full cells 1 and part cells 2.
		FPCGVoxelDenseMask Result = Source;
		TArray<int32> Origin; // per dense cell: the input cell that grew into it whole
		TArray<FPartialHit> Partials;
		if (bAnyLayers)
		{
			FPCGVoxelBoxSum Filled;
			Filled.Build(Source);
			ParallelFor(Grid.NumCells(), [&](int32 Index)
			{
				const FCellOffset& Offset = Offsets[Index];
				if (Offset.RemovesLayers() && !Filled.AllSet(Grid.Cells[Index], Offset.ShrinkLayers[0], Offset.ShrinkLayers[1]))
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
				if (!Offset.Grows() || Filled.AllSet(From, Offset.GrowReach[0], Offset.GrowReach[1]))
				{
					continue;
				}
				if (Origin.IsEmpty())
				{
					Origin.Init(INDEX_NONE, Count);
					Reserve.Init(-1.0f, Count);
					Distance.Init(0.0f, Count);
				}
				const FIntVector RLo = Offset.GrowReach[0];
				const FIntVector RHi = Offset.GrowReach[1];
				for (int32 DZ = -RLo.Z; DZ <= RHi.Z; ++DZ)
					for (int32 DY = -RLo.Y; DY <= RHi.Y; ++DY)
						for (int32 DX = -RLo.X; DX <= RHi.X; ++DX)
						{
							const FIntVector Cell = From + FIntVector(DX, DY, DZ);
							const int64 I = Source.Index(Cell);
							if (Source.Bits[I])
							{
								continue;
							}
							// The grower's box in this cell's frame (its centre is at -D), clipped to the cell.
							const FIntVector D(DX, DY, DZ);
							FVector Lo, Hi;
							bool bFull = true;
							bool bNone = false;
							for (int32 Axis = 0; Axis < 3; ++Axis)
							{
								const double Centre = -static_cast<double>(D[Axis]);
								// A side the grower shrinks on (a negative Amount Scale axis) pulls its growth in by the same
								// fraction, so cells grown sideways match the grower's shrunk height.
								Lo[Axis] = FMath::Max(-0.5, Centre - 0.5 - Offset.GrowMagnitude[0][Axis] + Offset.ShrinkFraction[0][Axis]);
								Hi[Axis] = FMath::Min(0.5, Centre + 0.5 + Offset.GrowMagnitude[1][Axis] - Offset.ShrinkFraction[1][Axis]);
								bNone |= Hi[Axis] - Lo[Axis] <= 1e-6;
								bFull &= Lo[Axis] <= -0.5 + 1e-9 && Hi[Axis] >= 0.5 - 1e-9;
							}
							if (bNone)
							{
								continue;
							}
							// Reach left over on the axes the offset grows along (an axis it does not grow along has no reach
							// to spare), measured in the direction of this cell.
							double SpareMin = TNumericLimits<double>::Max();
							for (int32 Axis = 0; Axis < 3; ++Axis)
							{
								const double MLo = Offset.GrowMagnitude[0][Axis];
								const double MHi = Offset.GrowMagnitude[1][Axis];
								if (MLo <= 0.0 && MHi <= 0.0)
								{
									continue;
								}
								const double M = D[Axis] > 0 ? MHi : (D[Axis] < 0 ? MLo : FMath::Max(MLo, MHi));
								SpareMin = FMath::Min(SpareMin, (M - FMath::Abs(D[Axis])) * ReserveWeight[Axis]);
							}
							if (SpareMin == TNumericLimits<double>::Max())
							{
								SpareMin = 0.0;
							}
							const float Spare = static_cast<float>(SpareMin);
							const float Dist = static_cast<float>((FVector(D) * Grid.VoxelSize).SizeSquared());
							if (!bFull)
							{
								Partials.Add({ I, Index, Lo, Hi, Spare, Dist });
								continue;
							}
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
			// Cells reached only in part, grouped by cell in dense order (growers stay in input order within a cell).
			Algo::StableSort(Partials, [](const FPartialHit& A, const FPartialHit& B) { return A.I < B.I; });
			for (const FPartialHit& Hit : Partials)
			{
				if (Result.Bits[Hit.I] == 0)
				{
					Result.Bits[Hit.I] = 2;
				}
			}
		}

		// ---- Result cells: whose values and whose offset each takes ----
		const bool bPush = Settings->RemovedCells == EPCGVoxelOffsetRemovedCells::Push;
		TArray<FIntVector> ResultCells;
		TArray<int32> OffsetOf; // index into Grid.Cells / Offsets
		TArray<int32> Own; // the In point copied, or INDEX_NONE when it needs the nearest old surface cell (Push)
		TArray<bool> Added;
		TArray<FIntPoint> PartialHits; // per result cell: its range in Partials, or empty when it is filled whole
		int32 Cursor = 0;
		for (int64 I = 0; I < Count; ++I)
		{
			if (!Result.Bits[I])
			{
				continue;
			}
			const FIntVector Cell = Result.Cell(I);
			if (Result.Bits[I] == 2)
			{
				while (Partials[Cursor].I < I) { ++Cursor; }
				int32 End = Cursor;
				while (End < Partials.Num() && Partials[End].I == I) { ++End; }
				ResultCells.Add(Cell);
				OffsetOf.Add(INDEX_NONE);
				Own.Add(Grid.CellPoint[Partials[Cursor].From]);
				Added.Add(true);
				PartialHits.Add(FIntPoint(Cursor, End));
				Cursor = End;
				continue;
			}
			PartialHits.Add(FIntPoint(0, 0));
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

		// A shrinking face moves in unless a whole cell is next to it; a part cell (a neighbour's growth) does not cover it.
		auto IsWhole = [&Result](const FIntVector& Cell) { return Result.Bits[Result.Index(Cell)] == 1; };

		// ---- Min Thickness: how far each shrinking cell's open faces move in, per axis ----
		// Along each axis the whole cells form runs; only a run's two end faces are open, so its thickness after the
		// shrink is its length less the two end cells' fractions. A run the fraction leaves under its minimum (the largest
		// of its cells') is clamped (end cells above 0 shrink less, sharing the cut by their fractions) or removed.
		TArray<FShrink> ShrinkFraction;
		ShrinkFraction.SetNum(ResultCells.Num());
		for (int32 Index = 0; Index < ResultCells.Num(); ++Index)
		{
			// Only the input's own cells shrink; cells added by growing are whole.
			if (OffsetOf[Index] != INDEX_NONE && !Added[Index])
			{
				ShrinkFraction[Index].Lo = Offsets[OffsetOf[Index]].ShrinkFraction[0];
				ShrinkFraction[Index].Hi = Offsets[OffsetOf[Index]].ShrinkFraction[1];
			}
		}
		TArray<bool> Clamped;
		TArray<bool> ThinRemoved;
		Clamped.Init(false, ResultCells.Num());
		ThinRemoved.Init(false, ResultCells.Num());
		if (Settings->bLimitThinness)
		{
			TArray<int32> ResultAt;
			ResultAt.Init(INDEX_NONE, Count);
			for (int32 Index = 0; Index < ResultCells.Num(); ++Index)
			{
				ResultAt[Result.Index(ResultCells[Index])] = Index;
			}
			auto MinOf = [&](int32 Index, int32 Axis)
			{
				return Own[Index] == INDEX_NONE ? 0.0 : MinThicknessValues.Get(Own[Index]) * UnitToCells[Axis];
			};
			const bool bRemove = Settings->ThinResult == EPCGVoxelOffsetThinResult::Remove;
			TArray<int32> Run;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (!AxisOn[Axis])
				{
					continue;
				}
				FIntVector Step(0);
				Step[Axis] = 1;
				for (int32 Start = 0; Start < ResultCells.Num(); ++Start)
				{
					const FIntVector& First = ResultCells[Start];
					if (!IsWhole(First) || IsWhole(First - Step))
					{
						continue;
					}
					Run.Reset();
					for (FIntVector Cell = First; IsWhole(Cell); Cell += Step) // the padding ends every run inside the box
					{
						Run.Add(ResultAt[Result.Index(Cell)]);
					}
					const int32 Lo = Run[0];
					const int32 Hi = Run.Last();
					const double FLo = ShrinkFraction[Lo].Lo[Axis];
					const double FHi = ShrinkFraction[Hi].Hi[Axis];
					if (FLo + FHi <= 0.0)
					{
						continue;
					}
					double Target = 0.0;
					for (const int32 Index : Run)
					{
						Target = FMath::Max(Target, MinOf(Index, Axis));
					}
					const double Thickness = Run.Num() - FLo - FHi;
					if (Thickness >= Target - 1e-9)
					{
						continue;
					}
					if (bRemove)
					{
						for (const int32 Index : Run)
						{
							ThinRemoved[Index] |= MinOf(Index, Axis) > 0.0;
						}
						continue;
					}
					const bool bLoFree = MinOf(Lo, Axis) > 0.0 && FLo > 0.0;
					const bool bHiFree = MinOf(Hi, Axis) > 0.0 && FHi > 0.0;
					const double Free = (bLoFree ? FLo : 0.0) + (bHiFree ? FHi : 0.0);
					if (Free <= 0.0)
					{
						continue;
					}
					const double Keep = FMath::Max(0.0, 1.0 - (Target - Thickness) / Free);
					if (bLoFree)
					{
						ShrinkFraction[Lo].Lo[Axis] = FLo * Keep;
						Clamped[Lo] = true;
					}
					if (bHiFree)
					{
						ShrinkFraction[Hi].Hi[Axis] = FHi * Keep;
						Clamped[Hi] = true;
					}
				}
			}
		}

		// ---- The fraction: per result cell, its box (or boxes) ----
		// Growing cells are whole; a cell reached only in part is the union of those parts (several points on one cell
		// when it takes more than one box, as at an inside corner). A shrinking cell moves only its open faces in. With
		// Split Inner Corners it is also carved near every empty neighbour; when that leaves anything but the face box,
		// the cell is written as its carved boxes instead. Several boxes on one cell go largest first, so a node reading
		// one point per cell gets the most of it. No box leaves its cell.
		TArray<FPiece> Pieces;
		TArray<bool> PieceSplit; // Was Split: one of several boxes on its cell
		TArray<bool> PieceShows; // a carved or part box: shows even when its cell's six neighbours are filled
		TArray<bool> PieceClamped; // Was Clamped: its cell's shrink was limited by Min Thickness
		Pieces.Reserve(ResultCells.Num());
		PieceClamped.Reserve(ResultCells.Num());
		PieceSplit.Reserve(ResultCells.Num());
		PieceShows.Reserve(ResultCells.Num());
		TArray<FPiece> Carved;
		TArray<FIntVector, TInlineAllocator<26>> EmptyNeighbours;
		auto VolumeOf = [](const FPiece& P) { const FVector Size = P.Hi - P.Lo; return Size.X * Size.Y * Size.Z; };
		auto AddSorted = [&](TArray<FPiece>& List, bool bClamped)
		{
			Algo::StableSort(List, [&](const FPiece& A, const FPiece& B) { return VolumeOf(A) > VolumeOf(B) + 1e-12; });
			for (const FPiece& Piece : List)
			{
				Pieces.Add(Piece);
				PieceSplit.Add(List.Num() > 1);
				PieceShows.Add(true);
				PieceClamped.Add(bClamped);
			}
		};
		for (int32 Index = 0; Index < ResultCells.Num(); ++Index)
		{
			const FIntVector& Cell = ResultCells[Index];
			if (PartialHits[Index].Y > PartialHits[Index].X)
			{
				const TConstArrayView<FPartialHit> Hits(Partials.GetData() + PartialHits[Index].X, PartialHits[Index].Y - PartialHits[Index].X);
				Carved.Reset();
				UnionHits(Hits, [&](int32 Hit, const FVector& Lo, const FVector& Hi)
				{
					FPiece& Piece = Carved.AddDefaulted_GetRef();
					Piece.Cell = Cell;
					Piece.Source = Grid.CellPoint[Hits[Hit].From];
					Piece.bFreshEntry = Settings->AddedValues == EPCGVoxelOffsetAddedValues::None;
					Piece.bAdded = true;
					Piece.Lo = Lo;
					Piece.Hi = Hi;
				});
				AddSorted(Carved, false);
				continue;
			}
			if (ThinRemoved[Index])
			{
				continue; // Min Thickness, Remove
			}
			const FShrink& Fraction = ShrinkFraction[Index];
			FPiece Whole;
			Whole.Cell = Cell;
			Whole.Source = Own[Index];
			Whole.bFreshEntry = Added[Index] && Settings->AddedValues == EPCGVoxelOffsetAddedValues::None;
			Whole.bAdded = Added[Index];

			if (!Fraction.IsZero())
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					FIntVector Step(0);
					Step[Axis] = 1;
					if (!IsWhole(Cell + Step)) { Whole.Hi[Axis] -= Fraction.Hi[Axis]; }
					if (!IsWhole(Cell - Step)) { Whole.Lo[Axis] += Fraction.Lo[Axis]; }
				}
				if ((Whole.Hi - Whole.Lo).GetMin() <= 1e-6)
				{
					continue; // shrunk to nothing: the cell is gone
				}

				if (Settings->bSplitInnerCorners)
				{
					EmptyNeighbours.Reset();
					for (int32 Z = -1; Z <= 1; ++Z)
						for (int32 Y = -1; Y <= 1; ++Y)
							for (int32 X = -1; X <= 1; ++X)
							{
								const FIntVector D(X, Y, Z);
								if (D != FIntVector::ZeroValue && !IsWhole(Cell + D))
								{
									EmptyNeighbours.Add(D);
								}
							}
					if (!EmptyNeighbours.IsEmpty())
					{
						FPiece Cube = Whole;
						Cube.Lo = FVector(-0.5);
						Cube.Hi = FVector(0.5);
						Carved.Reset();
						CarveCell(Cube, Fraction, EmptyNeighbours, Carved);
						const bool bSameAsFaces = Carved.Num() == 1
							&& Carved[0].Lo.Equals(Whole.Lo, 1e-9) && Carved[0].Hi.Equals(Whole.Hi, 1e-9);
						if (!bSameAsFaces && !Carved.IsEmpty())
						{
							AddSorted(Carved, Clamped[Index]);
							if (Carved.Num() == 1)
							{
								PieceSplit.Last() = true; // carved, even when one box is left
							}
							continue;
						}
					}
				}
			}

			Pieces.Add(Whole);
			PieceSplit.Add(false);
			PieceShows.Add(false);
			PieceClamped.Add(Clamped[Index]);
		}

		// ---- Compression: whole cells joined into chunks by the shared rules (Voxel/PCGVoxelChunking.h) ----
		bool bAnyChunk = false;
		if (Settings->bCompressAdded || Settings->bCompressEqualCells)
		{
			auto IsWholeBox = [](const FPiece& P) { return P.Lo.Equals(FVector(-0.5), 1e-9) && P.Hi.Equals(FVector(0.5), 1e-9); };
			// Whole cells merge as octree cubes and boxes; a part box (alone on its cell) joins only along an axis it fills,
			// with part boxes of exactly its shape, so no gap between them is swallowed.
			TArray<FIntVector> ChunkCells;
			TArray<int32> ChunkLabels; // piece index
			TArray<PCGVoxelChunking::FBox> PartBoxes;
			TArray<bool> InChunk;
			InChunk.Init(false, Pieces.Num());
			for (int32 Index = 0; Index < Pieces.Num(); ++Index)
			{
				const FPiece& P = Pieces[Index];
				if (PieceSplit[Index] || !(Settings->bCompressEqualCells || P.bAdded))
				{
					continue;
				}
				InChunk[Index] = true;
				if (IsWholeBox(P) && !PieceShows[Index])
				{
					ChunkCells.Add(P.Cell);
					ChunkLabels.Add(Index);
				}
				else
				{
					PartBoxes.Add({ P.Cell, FIntVector(1), Index });
				}
			}
			auto Equal = [&](int32 A, int32 B)
			{
				const FPiece& PA = Pieces[A];
				const FPiece& PB = Pieces[B];
				if (PieceClamped[A] != PieceClamped[B] || PA.bFreshEntry != PB.bFreshEntry)
				{
					return false;
				}
				if (PA.bFreshEntry)
				{
					return true; // both attribute defaults
				}
				if (Settings->bCompressEqualCells)
				{
					return ChunkEqual(PA.Source, PB.Source);
				}
				return PA.bAdded && PB.bAdded && PA.Source == PB.Source;
			};
			TArray<PCGVoxelChunking::FBox> Boxes;
			PCGVoxelChunking::MergeCells(ChunkCells, ChunkLabels, Settings->CompressMaxSpan, /*bJoinBoxes=*/true, Equal, Boxes);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				PCGVoxelChunking::JoinAlong(PartBoxes, Axis, Settings->CompressMaxSpan, [&](int32 A, int32 B)
				{
					const FPiece& PA = Pieces[A];
					const FPiece& PB = Pieces[B];
					return PA.Lo.Equals(PB.Lo, 1e-9) && PA.Hi.Equals(PB.Hi, 1e-9)
						&& PA.Lo[Axis] <= -0.5 + 1e-9 && PA.Hi[Axis] >= 0.5 - 1e-9
						&& PieceShows[A] == PieceShows[B] && Equal(A, B);
				});
			}
			Boxes.Append(PartBoxes);

			TArray<int32> Order; // into the old lists; chunks take their label's flags
			TArray<FPiece> NewPieces;
			for (int32 Index = 0; Index < Pieces.Num(); ++Index)
			{
				if (!InChunk[Index])
				{
					NewPieces.Add(Pieces[Index]);
					Order.Add(Index);
				}
			}
			for (const PCGVoxelChunking::FBox& Box : Boxes)
			{
				FPiece Chunk = Pieces[Box.Rep];
				Chunk.Cell = Box.First;
				Chunk.Span = Box.Span;
				// Relative to the chunk's centre: the span's box, moved in by the piece's own faces.
				Chunk.Lo = -FVector(Box.Span) * 0.5 + (Chunk.Lo + FVector(0.5));
				Chunk.Hi = FVector(Box.Span) * 0.5 + (Chunk.Hi - FVector(0.5));
				bAnyChunk |= Box.Span != FIntVector(1);
				NewPieces.Add(Chunk);
				Order.Add(Box.Rep);
			}
			// Back in cell order (Z, Y, X), several boxes on one cell keeping their order.
			TArray<int32> Sorted;
			Sorted.SetNumUninitialized(NewPieces.Num());
			for (int32 Index = 0; Index < Sorted.Num(); ++Index)
			{
				Sorted[Index] = Index;
			}
			Algo::StableSort(Sorted, [&](int32 A, int32 B) { return FPCGVoxelGrid::CellLess(NewPieces[A].Cell, NewPieces[B].Cell); });
			TArray<FPiece> FinalPieces;
			TArray<bool> FinalSplit, FinalShows, FinalClamped;
			for (const int32 Index : Sorted)
			{
				FinalPieces.Add(NewPieces[Index]);
				FinalSplit.Add(PieceSplit[Order[Index]]);
				FinalShows.Add(PieceShows[Order[Index]]);
				FinalClamped.Add(PieceClamped[Order[Index]]);
			}
			Pieces = MoveTemp(FinalPieces);
			PieceSplit = MoveTemp(FinalSplit);
			PieceShows = MoveTemp(FinalShows);
			PieceClamped = MoveTemp(FinalClamped);
		}

		// ---- Write: one point per piece, on its cell centre, bounds covering its box ----
		const FQuat GridRotation = Grid.GridToWorld.GetRotation();
		auto Emit = [&](const TArray<FPiece>& List, TArray<FPCGVoxelCellOut>& Outs) -> UPCGBasePointData*
		{
			Outs.Reset(List.Num());
			for (const FPiece& Piece : List)
			{
				Outs.Add({ Piece.Source, Piece.Cell, Piece.Span });
			}
			UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Outs, Settings->Grid.SpanAttribute, /*bWriteSpan=*/bAnyChunk);
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
			FPCGMetadataDomain* OutDomain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			for (int32 Index = 0; Index < List.Num(); ++Index)
			{
				const FPiece& Piece = List[Index];
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
			return Out;
		};
		// Only whole cells hide a neighbour's face; a part cell may leave it open.
		auto IsFilled = [&Result](const FIntVector& Cell) { return Result.Bits[Result.Index(Cell)] == 1; };

		TArray<FPCGVoxelCellOut> Outs;
		UPCGBasePointData* Out = Emit(Pieces, Outs);
		if (PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Outs, IsFilled))
		{
			// A carved or part box shows even when its cell's six neighbours are filled (the notch is diagonal).
			FPCGMetadataDomain* OutDomain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			if (FPCGMetadataAttribute<bool>* Surrounded = OutDomain
				? static_cast<FPCGMetadataAttribute<bool>*>(OutDomain->GetMutableAttribute(Settings->Grid.SurroundedAttribute)) : nullptr)
			{
				const TConstPCGValueRange<int64> Entries = Out->GetConstMetadataEntryValueRange();
				for (int32 Index = 0; Index < PieceShows.Num(); ++Index)
				{
					if (PieceShows[Index])
					{
						Surrounded->SetValue(Entries[Index], false);
					}
				}
			}
		}
		if (Settings->bLimitThinness && Settings->bWriteWasClamped)
		{
			PCGVoxelPointIO::WritePointAttribute<bool>(Out, Settings->WasClampedAttribute, false, PieceClamped);
		}
		if (Settings->bWriteWasSplit)
		{
			PCGVoxelPointIO::WritePointAttribute<bool>(Out, Settings->WasSplitAttribute, false, PieceSplit);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
