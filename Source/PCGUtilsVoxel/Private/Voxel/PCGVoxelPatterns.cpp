// Copyright Max Harris

#include "Voxel/PCGVoxelPatterns.h"

#include "Voxel/PCGVoxelChunking.h"

#define LOCTEXT_NAMESPACE "PCGVoxelPatterns"

namespace PCGVoxelPatternsPrivate
{
	/** Longest box side a pattern may ask for; keeps a typo from scanning millions of cells per fit. */
	const int32 MaxBoxSpan = 256;

	const TCHAR* const AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

	FText CellText(const FIntVector& Cell)
	{
		return FText::FromString(FString::Printf(TEXT("(%d, %d, %d)"), Cell.X, Cell.Y, Cell.Z));
	}

	bool BoxContains(const FPCGVoxelPatternBox& Box, const FIntVector& Cell)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Cell[Axis] < Box.First[Axis] || Cell[Axis] >= Box.First[Axis] + Box.Span[Axis])
			{
				return false;
			}
		}
		return true;
	}

	/** Cell I of a probe's row. */
	FIntVector ProbeCell(const FPCGVoxelPatternProbe& Probe, int32 I)
	{
		return Probe.Offset + PCGVoxelPatterns::DirectionVector(Probe.Step) * I;
	}

	/** One pattern under one turn, in grid directions from the origin cell. */
	struct FTurnedPattern
	{
		TArray<FIntVector> FilledProbes;
		TArray<FIntVector> EmptyProbes;
		TArray<FIntVector> UnclaimedProbes;
		TArray<FIntVector> SolidFaceProbes;
		/** Per Solid Face probe cell: the bit of the grid side that must be whole. */
		TArray<uint8> SolidFaceBits;
		/** Per box: its open sides as grid side bits. */
		TArray<uint8> BoxOpen;
		TArray<FIntVector> BoxFirst;
		TArray<FIntVector> BoxSpan;
		TArray<PCGVoxelRules::FRotation> MeshToGrid;
		/** Grid axes (bits) the pattern's Join Along axes land on. */
		int32 JoinAxes = 0;
	};

	int32 AxisOf(const FIntVector& Direction)
	{
		return Direction.X != 0 ? 0 : (Direction.Y != 0 ? 1 : 2);
	}

	/** The side bit (1 << EPCGVoxelPatternDirection) of a unit grid direction. */
	uint8 SideBit(const FIntVector& Direction)
	{
		const int32 Axis = AxisOf(Direction);
		return static_cast<uint8>(1 << (Axis * 2 + (Direction[Axis] < 0 ? 1 : 0)));
	}

	void Turn(const FPCGVoxelPattern& Pattern, const PCGVoxelRules::FRotation& Rotation, FTurnedPattern& Out)
	{
		for (const FPCGVoxelPatternProbe& Probe : Pattern.Probes)
		{
			TArray<FIntVector>& Cells = Probe.State == EPCGVoxelProbeState::Filled ? Out.FilledProbes
				: (Probe.State == EPCGVoxelProbeState::Unclaimed ? Out.UnclaimedProbes
				: (Probe.State == EPCGVoxelProbeState::SolidFace ? Out.SolidFaceProbes : Out.EmptyProbes));
			for (int32 I = 0; I < Probe.Count; ++I)
			{
				Cells.Add(Rotation.Apply(ProbeCell(Probe, I)));
				if (Probe.State == EPCGVoxelProbeState::SolidFace)
				{
					Out.SolidFaceBits.Add(SideBit(Rotation.Apply(PCGVoxelPatterns::DirectionVector(Probe.Face))));
				}
			}
		}
		for (const FPCGVoxelPatternBox& Box : Pattern.Boxes)
		{
			const FIntVector A = Rotation.Apply(Box.First);
			const FIntVector B = Rotation.Apply(Box.First + Box.Span - FIntVector(1));
			const FIntVector First(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
			const FIntVector Last(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
			Out.BoxFirst.Add(First);
			Out.BoxSpan.Add(Last - First + FIntVector(1));
			PCGVoxelRules::FRotation InPattern;
			PCGVoxelPatterns::BoxRotation(Box, InPattern);
			Out.MeshToGrid.Add(PCGVoxelPatterns::Compose(Rotation, InPattern));
			uint8 Open = 0;
			for (int32 Side = 0; Side < 6; ++Side)
			{
				if (Box.OpenSides & (1 << Side))
				{
					Open |= SideBit(Rotation.Apply(PCGVoxelPatterns::DirectionVector(static_cast<EPCGVoxelPatternDirection>(Side))));
				}
			}
			Out.BoxOpen.Add(Open);
		}
		const FIntVector Units[3] = { FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1) };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Pattern.JoinAlong & (1 << Axis))
			{
				Out.JoinAxes |= 1 << AxisOf(Rotation.Apply(Units[Axis]));
			}
		}
	}
}

FPCGVoxelPattern::FPCGVoxelPattern()
{
	Boxes.AddDefaulted();
}

FIntVector PCGVoxelPatterns::DirectionVector(EPCGVoxelPatternDirection Direction)
{
	switch (Direction)
	{
	case EPCGVoxelPatternDirection::PosX: return FIntVector(1, 0, 0);
	case EPCGVoxelPatternDirection::NegX: return FIntVector(-1, 0, 0);
	case EPCGVoxelPatternDirection::PosY: return FIntVector(0, 1, 0);
	case EPCGVoxelPatternDirection::NegY: return FIntVector(0, -1, 0);
	case EPCGVoxelPatternDirection::PosZ: return FIntVector(0, 0, 1);
	default: return FIntVector(0, 0, -1);
	}
}

PCGVoxelPatterns::FRotation PCGVoxelPatterns::Compose(const FRotation& A, const FRotation& B)
{
	FRotation Out;
	Out.X = A.Apply(B.X);
	Out.Y = A.Apply(B.Y);
	Out.Z = A.Apply(B.Z);
	return Out;
}

bool PCGVoxelPatterns::BoxRotation(const FPCGVoxelPatternBox& Box, FRotation& OutRotation)
{
	const FIntVector Y = DirectionVector(Box.MeshForward);
	const FIntVector Z = DirectionVector(Box.MeshUp);
	if (Y.X * Z.X + Y.Y * Z.Y + Z.Z * Y.Z != 0)
	{
		return false;
	}
	OutRotation.Y = Y;
	OutRotation.Z = Z;
	OutRotation.X = FIntVector(Y.Y * Z.Z - Y.Z * Z.Y, Y.Z * Z.X - Y.X * Z.Z, Y.X * Z.Y - Y.Y * Z.X);
	return true;
}

bool PCGVoxelPatterns::Validate(const FPCGVoxelPattern& Pattern, FText& OutError)
{
	using namespace PCGVoxelPatternsPrivate;
	const bool bAdd = Pattern.Operation == EPCGVoxelPatternOperation::Add;
	const UEnum* DirectionEnum = StaticEnum<EPCGVoxelPatternDirection>();

	if (Pattern.Boxes.IsEmpty())
	{
		OutError = LOCTEXT("NoBoxes", "Boxes is empty; a pattern needs at least one box to make a chunk.");
		return false;
	}
	if (Pattern.MaxJoinedSpan < 0)
	{
		OutError = FText::Format(LOCTEXT("NegativeJoin", "Max Joined Span is {0}; it must be 0 (no limit) or more."), Pattern.MaxJoinedSpan);
		return false;
	}
	if (Pattern.JoinAlong & ~static_cast<int32>(EPCGVoxelAxes::All))
	{
		OutError = FText::Format(LOCTEXT("BadJoinAxes", "Join Along is {0}; only the X (1), Y (2) and Z (4) bits are allowed."), Pattern.JoinAlong);
		return false;
	}

	for (int32 BoxIndex = 0; BoxIndex < Pattern.Boxes.Num(); ++BoxIndex)
	{
		const FPCGVoxelPatternBox& Box = Pattern.Boxes[BoxIndex];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Box.Span[Axis] < 1)
			{
				OutError = FText::Format(LOCTEXT("SmallSpan", "Box {0}: Span {1} is {2}; it must be at least 1."),
					BoxIndex, FText::FromString(AxisNames[Axis]), Box.Span[Axis]);
				return false;
			}
			if (Box.Span[Axis] > MaxBoxSpan)
			{
				OutError = FText::Format(LOCTEXT("BigSpan", "Box {0}: Span {1} is {2}; the most a box may cover along one axis is {3} (use Join Along for longer chunks)."),
					BoxIndex, FText::FromString(AxisNames[Axis]), Box.Span[Axis], MaxBoxSpan);
				return false;
			}
		}
		if (Box.OpenSides & ~0x3F)
		{
			OutError = FText::Format(LOCTEXT("BadOpenSides", "Box {0}: Open Sides is {1}; only the six side bits (1 to 32) are allowed."),
				BoxIndex, Box.OpenSides);
			return false;
		}
		if (Box.MeshIndex < 0)
		{
			OutError = FText::Format(LOCTEXT("NegativeMesh", "Box {0}: Mesh Index is {1}; it must be 0 or more (-1 marks cells no pattern took)."),
				BoxIndex, Box.MeshIndex);
			return false;
		}
		FRotation Unused;
		if (!BoxRotation(Box, Unused))
		{
			OutError = FText::Format(LOCTEXT("BadMeshAxes", "Box {0}: Mesh Forward ({1}) and Mesh Up ({2}) are on the same axis; they must be at a right angle."),
				BoxIndex, DirectionEnum->GetDisplayNameTextByValue(static_cast<int64>(Box.MeshForward)),
				DirectionEnum->GetDisplayNameTextByValue(static_cast<int64>(Box.MeshUp)));
			return false;
		}
		if (bAdd && BoxContains(Box, FIntVector::ZeroValue))
		{
			OutError = FText::Format(LOCTEXT("AddOnOrigin", "Box {0} covers the origin cell (0, 0, 0), which is always filled; the boxes of an Add pattern must be in empty cells."),
				BoxIndex);
			return false;
		}
		for (int32 Other = 0; Other < BoxIndex; ++Other)
		{
			const FPCGVoxelPatternBox& B = Pattern.Boxes[Other];
			bool bOverlap = true;
			FIntVector Shared;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 Lo = FMath::Max(Box.First[Axis], B.First[Axis]);
				const int32 Hi = FMath::Min(Box.First[Axis] + Box.Span[Axis], B.First[Axis] + B.Span[Axis]);
				bOverlap &= Lo < Hi;
				Shared[Axis] = Lo;
			}
			if (bOverlap)
			{
				OutError = FText::Format(LOCTEXT("BoxOverlap", "Box {0} and box {1} both cover cell {2}; the boxes of one pattern may not overlap."),
					Other, BoxIndex, CellText(Shared));
				return false;
			}
		}
	}

	for (int32 ProbeIndex = 0; ProbeIndex < Pattern.Probes.Num(); ++ProbeIndex)
	{
		const FPCGVoxelPatternProbe& Probe = Pattern.Probes[ProbeIndex];
		const bool bEmpty = Probe.State == EPCGVoxelProbeState::Empty;
		const FText StateText = StaticEnum<EPCGVoxelProbeState>()->GetDisplayNameTextByValue(static_cast<int64>(Probe.State));
		if (Probe.Count < 1)
		{
			OutError = FText::Format(LOCTEXT("SmallCount", "Probe {0}: Count is {1}; it must be at least 1."), ProbeIndex, Probe.Count);
			return false;
		}
		if (Probe.Count > MaxBoxSpan)
		{
			OutError = FText::Format(LOCTEXT("BigCount", "Probe {0}: Count is {1}; the most cells one probe may cover is {2}."),
				ProbeIndex, Probe.Count, MaxBoxSpan);
			return false;
		}
		for (int32 CellIndex = 0; CellIndex < Probe.Count; ++CellIndex)
		{
			const FIntVector Cell = ProbeCell(Probe, CellIndex);
			if (bEmpty && Cell == FIntVector::ZeroValue)
			{
				OutError = FText::Format(LOCTEXT("EmptyOrigin", "Probe {0} wants the origin cell (0, 0, 0) empty, but the origin is always a filled cell; the pattern could never fit."),
					ProbeIndex);
				return false;
			}
			for (int32 Other = 0; Other < ProbeIndex; ++Other)
			{
				const FPCGVoxelPatternProbe& B = Pattern.Probes[Other];
				if ((B.State == EPCGVoxelProbeState::Empty) == bEmpty)
				{
					continue;
				}
				for (int32 OtherCell = 0; OtherCell < FMath::Min(B.Count, MaxBoxSpan); ++OtherCell)
				{
					if (ProbeCell(B, OtherCell) == Cell)
					{
						OutError = FText::Format(LOCTEXT("ProbeClash", "Probe {0} and probe {1} are both on cell {2}, one wanting it empty and one not; the pattern could never fit."),
							Other, ProbeIndex, CellText(Cell));
						return false;
					}
				}
			}
			for (int32 BoxIndex = 0; BoxIndex < Pattern.Boxes.Num(); ++BoxIndex)
			{
				if (!BoxContains(Pattern.Boxes[BoxIndex], Cell))
				{
					continue;
				}
				if (bAdd && !bEmpty)
				{
					OutError = FText::Format(LOCTEXT("FilledInAdd", "Probe {0} wants cell {1} {3}, but box {2} of this Add pattern adds that cell, so it must be empty; the pattern could never fit."),
						ProbeIndex, CellText(Cell), BoxIndex, StateText);
					return false;
				}
				if (!bAdd && bEmpty)
				{
					OutError = FText::Format(LOCTEXT("EmptyInClaim", "Probe {0} wants cell {1} empty, but box {2} of this Claim pattern takes that cell, so it must be filled; the pattern could never fit."),
						ProbeIndex, CellText(Cell), BoxIndex);
					return false;
				}
			}
		}
	}
	return true;
}

void PCGVoxelPatterns::Apply(const FPCGVoxelGrid& Grid, TConstArrayView<const FPCGVoxelPattern*> Patterns, const FLimits& Limits, FResult& OutResult)
{
	using namespace PCGVoxelPatternsPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGVoxelPatterns::Apply);

	OutResult.Chunks.Reset();
	OutResult.Added.Reset();
	OutResult.Claimed.Init(false, Grid.NumCells());
	OutResult.NumFits.Init(0, Patterns.Num());

	const TArray<FRotation>& Rotations = PCGVoxelRules::Rotations();
	TSet<FIntVector> Pending;
	// Per entry of Grid.Cells, for claimed cells: the pattern that took it and its chunk's open sides (grid side bits).
	TArray<int32> ClaimedBy;
	TArray<uint8> ClaimedOpen;
	ClaimedBy.Init(INDEX_NONE, Grid.NumCells());
	ClaimedOpen.Init(0, Grid.NumCells());
	// The same for added cells, of patterns that have finished.
	TMap<FIntVector, uint8> AddedOpen;
	TMap<FIntVector, uint8> PendingOpen;

	for (int32 PatternIndex = 0; PatternIndex < Patterns.Num(); ++PatternIndex)
	{
		const FPCGVoxelPattern& Pattern = *Patterns[PatternIndex];
		const bool bAdd = Pattern.Operation == EPCGVoxelPatternOperation::Add;
		const bool bSeeAdded = Pattern.bProbesSeeAddedCells && !OutResult.Added.IsEmpty();

		TArray<FTurnedPattern> Turns;
		Turns.SetNum(PCGVoxelRules::NumTurns(Pattern.Turns));
		for (int32 TurnIndex = 0; TurnIndex < Turns.Num(); ++TurnIndex)
		{
			Turn(Pattern, Rotations[TurnIndex], Turns[TurnIndex]);
		}

		// Unclaimed and Solid Face probes read the claims of earlier patterns only, never this pattern's own: two fits
		// of one pattern do not block each other, so the result does not depend on which cell was visited first.
		auto IsPlain = [&](int32 CellIndex)
		{
			return ClaimedBy[CellIndex] == INDEX_NONE || ClaimedBy[CellIndex] == PatternIndex;
		};
		auto IsSolidOn = [&](const FIntVector& Cell, uint8 Side)
		{
			const int32 CellIndex = Grid.FindCellIndex(Cell);
			if (CellIndex != INDEX_NONE)
			{
				return IsPlain(CellIndex) || (ClaimedOpen[CellIndex] & Side) == 0;
			}
			const uint8* Open = bSeeAdded ? AddedOpen.Find(Cell) : nullptr;
			return Open && (*Open & Side) == 0;
		};

		auto IsFilled = [&](const FIntVector& Cell)
		{
			return Grid.IsOccupied(Cell) || (bSeeAdded && OutResult.Added.Contains(Cell));
		};

		// Is every cell of this box free to take?
		auto BoxFree = [&](const FIntVector& First, const FIntVector& Span)
		{
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						const FIntVector Cell = First + FIntVector(X, Y, Z);
						if (bAdd)
						{
							if (Grid.IsOccupied(Cell) || OutResult.Added.Contains(Cell) || Pending.Contains(Cell))
							{
								return false;
							}
						}
						else
						{
							const int32 CellIndex = Grid.FindCellIndex(Cell);
							if (CellIndex == INDEX_NONE || OutResult.Claimed[CellIndex])
							{
								return false;
							}
						}
						if (Limits.CanEdit && !Limits.CanEdit(PatternIndex, Cell))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		// One entry per box made; a chunking box's Rep indexes these.
		struct FMade
		{
			int32 Turn;
			int32 Box;
			int32 SourcePoint;
		};
		TArray<FMade> Made;
		TArray<PCGVoxelChunking::FBox> Boxes;

		for (int32 OriginIndex = 0; OriginIndex < Grid.NumCells(); ++OriginIndex)
		{
			const FIntVector Origin = Grid.Cells[OriginIndex];
			if (Limits.CanStart && !Limits.CanStart(PatternIndex, Origin))
			{
				continue;
			}
			for (int32 TurnIndex = 0; TurnIndex < Turns.Num(); ++TurnIndex)
			{
				const FTurnedPattern& Turned = Turns[TurnIndex];
				bool bFits = true;
				for (int32 I = 0; bFits && I < Turned.EmptyProbes.Num(); ++I)
				{
					bFits = !IsFilled(Origin + Turned.EmptyProbes[I]);
				}
				for (int32 I = 0; bFits && I < Turned.FilledProbes.Num(); ++I)
				{
					bFits = IsFilled(Origin + Turned.FilledProbes[I]);
				}
				for (int32 I = 0; bFits && I < Turned.UnclaimedProbes.Num(); ++I)
				{
					const int32 CellIndex = Grid.FindCellIndex(Origin + Turned.UnclaimedProbes[I]);
					bFits = CellIndex != INDEX_NONE && IsPlain(CellIndex);
				}
				for (int32 I = 0; bFits && I < Turned.SolidFaceProbes.Num(); ++I)
				{
					bFits = IsSolidOn(Origin + Turned.SolidFaceProbes[I], Turned.SolidFaceBits[I]);
				}
				for (int32 I = 0; bFits && I < Turned.BoxFirst.Num(); ++I)
				{
					bFits = BoxFree(Origin + Turned.BoxFirst[I], Turned.BoxSpan[I]);
				}
				if (!bFits)
				{
					continue;
				}

				++OutResult.NumFits[PatternIndex];
				for (int32 BoxIndex = 0; BoxIndex < Turned.BoxFirst.Num(); ++BoxIndex)
				{
					const FIntVector First = Origin + Turned.BoxFirst[BoxIndex];
					const FIntVector Span = Turned.BoxSpan[BoxIndex];
					for (int32 Z = 0; Z < Span.Z; ++Z)
					{
						for (int32 Y = 0; Y < Span.Y; ++Y)
						{
							for (int32 X = 0; X < Span.X; ++X)
							{
								const FIntVector Cell = First + FIntVector(X, Y, Z);
								if (bAdd)
								{
									Pending.Add(Cell);
									PendingOpen.Add(Cell, Turned.BoxOpen[BoxIndex]);
								}
								else
								{
									const int32 CellIndex = Grid.FindCellIndex(Cell);
									OutResult.Claimed[CellIndex] = true;
									ClaimedBy[CellIndex] = PatternIndex;
									ClaimedOpen[CellIndex] = Turned.BoxOpen[BoxIndex];
								}
							}
						}
					}
					PCGVoxelChunking::FBox& Box = Boxes.AddDefaulted_GetRef();
					Box.First = First;
					Box.Span = Span;
					Box.Rep = Made.Add({ TurnIndex, BoxIndex, bAdd ? Grid.CellPoint[OriginIndex] : Grid.FindPoint(First) });
				}
			}
		}

		// Longer chunks, by the shared chunking: fits of the same turn and box lined up end to end.
		if (Pattern.JoinAlong != 0)
		{
			const int32 MaxSpan = Pattern.MaxJoinedSpan > 0 ? Pattern.MaxJoinedSpan : MAX_int32;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				PCGVoxelChunking::JoinAlong(Boxes, Axis, MaxSpan, [&](int32 A, int32 B)
				{
					return Made[A].Turn == Made[B].Turn && Made[A].Box == Made[B].Box && (Turns[Made[A].Turn].JoinAxes & (1 << Axis)) != 0;
				});
			}
		}
		Boxes.Sort([](const PCGVoxelChunking::FBox& A, const PCGVoxelChunking::FBox& B) { return FPCGVoxelGrid::CellLess(A.First, B.First); });

		for (const PCGVoxelChunking::FBox& Box : Boxes)
		{
			const FMade& From = Made[Box.Rep];
			FChunk& Chunk = OutResult.Chunks.AddDefaulted_GetRef();
			Chunk.First = Box.First;
			Chunk.Span = Box.Span;
			Chunk.Pattern = PatternIndex;
			Chunk.MeshIndex = Pattern.Boxes[From.Box].MeshIndex;
			Chunk.SourcePoint = From.SourcePoint;
			Chunk.MeshToGrid = Turns[From.Turn].MeshToGrid[From.Box];
			Chunk.bAdded = bAdd;
		}

		// Added cells show (to patterns that ask) only once the whole pattern has run.
		OutResult.Added.Append(Pending);
		Pending.Reset();
		AddedOpen.Append(PendingOpen);
		PendingOpen.Reset();
	}
}

void PCGVoxelPatterns::Remainder(const FPCGVoxelGrid& Grid, const FResult& Result, TArray<FPCGVoxelCellOut>& OutCells)
{
	for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
	{
		if (!Grid.PointUsed[Point])
		{
			continue;
		}
		const FIntVector First = Grid.PointFirst[Point];
		const FIntVector Span = Grid.PointSpan[Point];
		TArray<FIntVector, TInlineAllocator<8>> Free;
		bool bWhole = true;
		for (int32 Z = 0; Z < Span.Z; ++Z)
		{
			for (int32 Y = 0; Y < Span.Y; ++Y)
			{
				for (int32 X = 0; X < Span.X; ++X)
				{
					const FIntVector Cell = First + FIntVector(X, Y, Z);
					const int32 CellIndex = Grid.FindCellIndex(Cell);
					if (CellIndex != INDEX_NONE && Result.Claimed[CellIndex])
					{
						bWhole = false;
					}
					else
					{
						Free.Add(Cell);
					}
				}
			}
		}
		if (bWhole)
		{
			OutCells.Add({ Point, First, Span });
		}
		else
		{
			for (const FIntVector& Cell : Free)
			{
				OutCells.Add({ Point, Cell, FIntVector(1) });
			}
		}
	}
}

void PCGVoxelPatterns::ToMeshFrameBounds(const FRotation& MeshToGrid, const FVector& GridMin, const FVector& GridMax, FVector& OutMin, FVector& OutMax)
{
	const FIntVector Axes[3] = { MeshToGrid.X, MeshToGrid.Y, MeshToGrid.Z };
	for (int32 MeshAxis = 0; MeshAxis < 3; ++MeshAxis)
	{
		const FIntVector& Direction = Axes[MeshAxis];
		const int32 GridAxis = PCGVoxelPatternsPrivate::AxisOf(Direction);
		if (Direction[GridAxis] > 0)
		{
			OutMin[MeshAxis] = GridMin[GridAxis];
			OutMax[MeshAxis] = GridMax[GridAxis];
		}
		else
		{
			OutMin[MeshAxis] = -GridMax[GridAxis];
			OutMax[MeshAxis] = -GridMin[GridAxis];
		}
	}
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::TopEdge()
{
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("TopEdge");
	Pattern.DebugColor = FLinearColor(0.1f, 0.6f, 1.0f);
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(0, 0, 1), EPCGVoxelProbeState::Empty });
	Pattern.Boxes[0].OpenSides = (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosX)) | (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosZ));
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	Pattern.JoinAlong = static_cast<int32>(EPCGVoxelAxes::Y);
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::UprightCorner(int32 Size, int32 WallCells)
{
	WallCells = FMath::Clamp(WallCells, 0, PCGVoxelPatternsPrivate::MaxBoxSpan);
	Size = FMath::Clamp(Size, 1, PCGVoxelPatternsPrivate::MaxBoxSpan);
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("UprightCorner");
	Pattern.DebugColor = FLinearColor(1.0f, 0.5f, 0.0f);
	// The origin is the corner cell itself; the box reaches back from it, so the pattern turns about the corner.
	FPCGVoxelPatternBox& Box = Pattern.Boxes[0];
	Box.First = FIntVector(1 - Size, 1 - Size, 0);
	Box.Span = FIntVector(Size, Size, 1);
	Box.OpenSides = (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosX)) | (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosY));
	// The edge mesh stood on end: its +X stays +X, its +Z (the other open side) points along +Y.
	Box.MeshForward = EPCGVoxelPatternDirection::NegZ;
	Box.MeshUp = EPCGVoxelPatternDirection::PosY;
	for (int32 I = 0; I < Size; ++I)
	{
		Pattern.Probes.Add({ FIntVector(1, -I, 0), EPCGVoxelProbeState::Empty });
		Pattern.Probes.Add({ FIntVector(-I, 1, 0), EPCGVoxelProbeState::Empty });
	}
	// Both walls must run on from the corner as plain cells, over the corner's full width: a wall's end, a lone block
	// and a spot beside another pattern's chunk are not corners.
	for (int32 I = 0; WallCells > 0 && I < Size; ++I)
	{
		Pattern.Probes.Add({ FIntVector(-Size, -I, 0), EPCGVoxelProbeState::Unclaimed, WallCells, EPCGVoxelPatternDirection::NegX });
		Pattern.Probes.Add({ FIntVector(-I, -Size, 0), EPCGVoxelProbeState::Unclaimed, WallCells, EPCGVoxelPatternDirection::NegY });
	}
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	Pattern.JoinAlong = static_cast<int32>(EPCGVoxelAxes::Z);
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::InsideCornerFill(int32 WallCells)
{
	WallCells = FMath::Clamp(WallCells, 1, PCGVoxelPatternsPrivate::MaxBoxSpan);
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("InsideCornerFill");
	Pattern.DebugColor = FLinearColor(1.0f, 0.1f, 0.5f);
	Pattern.Operation = EPCGVoxelPatternOperation::Add;
	// The origin is the cell in the bend, diagonal to the empty corner; it may be anything (a corner chunk's solid
	// corner included). The walls beside the corner must show a whole side toward the fill: plain cells or the solid
	// back of a chunk, never a chunk's cut side.
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::SolidFace, WallCells, EPCGVoxelPatternDirection::PosX, EPCGVoxelPatternDirection::PosY });
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::SolidFace, WallCells, EPCGVoxelPatternDirection::PosY, EPCGVoxelPatternDirection::PosX });
	Pattern.Probes.Add({ FIntVector(1, 1, 0), EPCGVoxelProbeState::Empty });
	FPCGVoxelPatternBox& Box = Pattern.Boxes[0];
	Box.First = FIntVector(1, 1, 0);
	Box.OpenSides = (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosX)) | (1 << static_cast<int32>(EPCGVoxelPatternDirection::PosY));
	// The edge mesh stood on end, its two whole faces against the filled cells beside the corner.
	Box.MeshForward = EPCGVoxelPatternDirection::NegZ;
	Box.MeshUp = EPCGVoxelPatternDirection::PosY;
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	Pattern.JoinAlong = static_cast<int32>(EPCGVoxelAxes::Z);
	return Pattern;
}

namespace PCGVoxelPatternsPrivate
{
	int32 Sides(std::initializer_list<EPCGVoxelPatternDirection> Directions)
	{
		int32 Bits = 0;
		for (const EPCGVoxelPatternDirection Direction : Directions)
		{
			Bits |= 1 << static_cast<int32>(Direction);
		}
		return Bits;
	}

	// The colours of the designer's sketch: slopes green, outside corners pink, inside corners light grey.
	const FLinearColor ChamferEdgeColor(0.35f, 0.75f, 0.3f);
	const FLinearColor ChamferOutsideColor(0.85f, 0.4f, 0.8f);
	const FLinearColor ChamferInsideColor(0.8f, 0.8f, 0.8f);
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::TopOutsideCorner()
{
	using namespace PCGVoxelPatternsPrivate;
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("TopOutsideCorner");
	Pattern.DebugColor = ChamferOutsideColor;
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(0, 0, 1), EPCGVoxelProbeState::Empty });
	Pattern.Boxes[0].MeshIndex = OutsideCornerMesh;
	Pattern.Boxes[0].OpenSides = Sides({ EPCGVoxelPatternDirection::PosX, EPCGVoxelPatternDirection::PosY, EPCGVoxelPatternDirection::PosZ });
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::TopInsideCorner()
{
	using namespace PCGVoxelPatternsPrivate;
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("TopInsideCorner");
	Pattern.DebugColor = ChamferInsideColor;
	// The two neighbours are the edge cells whose slopes meet here; the cell on their diagonal is the open corner.
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Filled });
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Filled });
	Pattern.Probes.Add({ FIntVector(1, 1, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(0, 0, 1), EPCGVoxelProbeState::Empty });
	Pattern.Boxes[0].MeshIndex = InsideCornerMesh;
	Pattern.Boxes[0].OpenSides = Sides({ EPCGVoxelPatternDirection::PosZ });
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::SkirtEdge()
{
	using namespace PCGVoxelPatternsPrivate;
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("SkirtEdge");
	Pattern.DebugColor = ChamferEdgeColor;
	Pattern.Operation = EPCGVoxelPatternOperation::Add;
	// The origin is the wall cell. The new cell is beside it, the Edge mesh as authored: whole against the wall (-X)
	// and on the ground (-Z), sloping away. An Add box only fits empty cells, so no probe is needed.
	FPCGVoxelPatternBox& Box = Pattern.Boxes[0];
	Box.First = FIntVector(1, 0, 0);
	Box.MeshIndex = EdgeMesh;
	Box.OpenSides = Sides({ EPCGVoxelPatternDirection::PosX, EPCGVoxelPatternDirection::PosZ });
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	Pattern.JoinAlong = static_cast<int32>(EPCGVoxelAxes::Y);
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::SkirtOutsideCorner()
{
	using namespace PCGVoxelPatternsPrivate;
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("SkirtOutsideCorner");
	Pattern.DebugColor = ChamferOutsideColor;
	Pattern.Operation = EPCGVoxelPatternOperation::Add;
	// The origin is the corner cell of the shape; the new cell is on its diagonal, between the two skirt edges.
	// Probes read the input set, so the edge cells an earlier pattern added there still read as empty.
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Empty });
	FPCGVoxelPatternBox& Box = Pattern.Boxes[0];
	Box.First = FIntVector(1, 1, 0);
	Box.MeshIndex = OutsideCornerMesh;
	Box.OpenSides = Sides({ EPCGVoxelPatternDirection::PosX, EPCGVoxelPatternDirection::PosY, EPCGVoxelPatternDirection::PosZ });
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Presets::SkirtInsideCorner()
{
	using namespace PCGVoxelPatternsPrivate;
	FPCGVoxelPattern Pattern;
	Pattern.Name = TEXT("SkirtInsideCorner");
	Pattern.DebugColor = ChamferInsideColor;
	Pattern.Operation = EPCGVoxelPatternOperation::Add;
	// The origin is the cell in the bend; the new cell is the empty one between the two walls.
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Filled });
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Filled });
	FPCGVoxelPatternBox& Box = Pattern.Boxes[0];
	Box.First = FIntVector(1, 1, 0);
	Box.MeshIndex = InsideCornerMesh;
	Box.OpenSides = Sides({ EPCGVoxelPatternDirection::PosZ });
	Pattern.Turns = EPCGVoxelRuleTurns::AboutUp;
	return Pattern;
}

FPCGVoxelPattern PCGVoxelPatterns::Scaled(const FPCGVoxelPattern& Pattern, const FIntVector& InScale)
{
	using namespace PCGVoxelPatternsPrivate;
	const FIntVector Scale(FMath::Max(InScale.X, 1), FMath::Max(InScale.Y, 1), FMath::Max(InScale.Z, 1));
	if (Scale == FIntVector(1))
	{
		return Pattern;
	}

	FPCGVoxelPattern Out = Pattern;
	for (FPCGVoxelPatternBox& Box : Out.Boxes)
	{
		Box.First = FIntVector(Box.First.X * Scale.X, Box.First.Y * Scale.Y, Box.First.Z * Scale.Z);
		Box.Span = FIntVector(Box.Span.X * Scale.X, Box.Span.Y * Scale.Y, Box.Span.Z * Scale.Z);
	}

	// A probe row of Count cells becomes Count x Scale cells along its step, repeated across the block's other two axes.
	Out.Probes.Reset();
	for (const FPCGVoxelPatternProbe& Probe : Pattern.Probes)
	{
		const FIntVector Step = DirectionVector(Probe.Step);
		const int32 Along = AxisOf(Step);
		const int32 B = (Along + 1) % 3;
		const int32 C = (Along + 2) % 3;
		FIntVector Start(Probe.Offset.X * Scale.X, Probe.Offset.Y * Scale.Y, Probe.Offset.Z * Scale.Z);
		if (Step[Along] < 0)
		{
			// The row runs toward minus: it starts at the far end of its first block.
			Start[Along] += Scale[Along] - 1;
		}
		for (int32 J = 0; J < Scale[B]; ++J)
		{
			for (int32 K = 0; K < Scale[C]; ++K)
			{
				FPCGVoxelPatternProbe& Row = Out.Probes.Add_GetRef(Probe);
				Row.Offset = Start;
				Row.Offset[B] += J;
				Row.Offset[C] += K;
				Row.Count = Probe.Count * Scale[Along];
			}
		}
	}

	// The rest of the origin block is filled, as the origin cell is, where nothing else says what it is.
	auto IsCovered = [&Out](const FIntVector& Cell)
	{
		for (const FPCGVoxelPatternBox& Box : Out.Boxes)
		{
			if (BoxContains(Box, Cell))
			{
				return true;
			}
		}
		for (const FPCGVoxelPatternProbe& Probe : Out.Probes)
		{
			for (int32 I = 0; I < FMath::Min(Probe.Count, MaxBoxSpan); ++I)
			{
				if (ProbeCell(Probe, I) == Cell)
				{
					return true;
				}
			}
		}
		return false;
	};
	TArray<FPCGVoxelPatternProbe> OriginBlock;
	for (int32 Z = 0; Z < Scale.Z; ++Z)
	{
		for (int32 Y = 0; Y < Scale.Y; ++Y)
		{
			for (int32 X = 0; X < Scale.X; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				if (Cell != FIntVector::ZeroValue && !IsCovered(Cell))
				{
					OriginBlock.Add({ Cell, EPCGVoxelProbeState::Filled });
				}
			}
		}
	}
	Out.Probes.Append(OriginBlock);
	return Out;
}

#undef LOCTEXT_NAMESPACE
