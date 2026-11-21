// Copyright Max Harris
//
// Voxel patterns: authored chunk layouts, order and claiming, turns, joining, added cells, validation, limits, and the
// Voxel | Apply Patterns node (turned points keep their grid-axis span; bounds, attributes, colour, unmatched cells).

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Elements/PCGVoxelApplyPatterns.h"
#include "Elements/PCGVoxelPattern.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelPatterns.h"

namespace PCGVoxelPatternsTests
{
	using namespace PCGVoxelTestHelpers;

	/** A grid of single cells, point i on Cells[i]. */
	FPCGVoxelGrid MakeGrid(const TArray<FIntVector>& Cells)
	{
		FPCGVoxelGrid Grid;
		Grid.PointFirst = Cells;
		Grid.PointSpan.Init(FIntVector(1), Cells.Num());
		Grid.PointUsed.Init(true, Cells.Num());
		Grid.BuildCells();
		return Grid;
	}

	PCGVoxelPatterns::FResult ApplyTo(const FPCGVoxelGrid& Grid, const TArray<FPCGVoxelPattern>& Patterns,
		const PCGVoxelPatterns::FLimits& Limits = PCGVoxelPatterns::FLimits())
	{
		TArray<const FPCGVoxelPattern*> Pointers;
		for (const FPCGVoxelPattern& Pattern : Patterns)
		{
			Pointers.Add(&Pattern);
		}
		PCGVoxelPatterns::FResult Result;
		PCGVoxelPatterns::Apply(Grid, Pointers, Limits, Result);
		return Result;
	}

	/** Every cell covered by the chunks; false in bNoOverlap when two chunks share one. */
	TSet<FIntVector> Covered(const PCGVoxelPatterns::FResult& Result, bool& bNoOverlap)
	{
		TSet<FIntVector> Cells;
		bNoOverlap = true;
		for (const PCGVoxelPatterns::FChunk& Chunk : Result.Chunks)
		{
			for (int32 Z = 0; Z < Chunk.Span.Z; ++Z)
				for (int32 Y = 0; Y < Chunk.Span.Y; ++Y)
					for (int32 X = 0; X < Chunk.Span.X; ++X)
					{
						bool bAlready = false;
						Cells.Add(Chunk.First + FIntVector(X, Y, Z), &bAlready);
						bNoOverlap &= !bAlready;
					}
		}
		return Cells;
	}

	int32 CountOf(const PCGVoxelPatterns::FResult& Result, int32 Pattern)
	{
		return Result.Chunks.FilterByPredicate([&](const PCGVoxelPatterns::FChunk& C) { return C.Pattern == Pattern; }).Num();
	}

	FPCGVoxelPattern Strip(const FIntVector& Span)
	{
		FPCGVoxelPattern Pattern;
		Pattern.Boxes[0].Span = Span;
		Pattern.Turns = EPCGVoxelRuleTurns::Exact;
		return Pattern;
	}

	template <typename T>
	T ValueAt(const UPCGBasePointData* Points, FName Name, int32 Index, const T& Missing)
	{
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Missing;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}

	struct FNodeResult
	{
		const UPCGBasePointData* Out = nullptr;
		const UPCGBasePointData* Unmatched = nullptr;
		bool bOutKeptTag = false;
	};

	FNodeResult RunNode(const UPCGData* In, const TArray<TPair<FPCGVoxelPattern, int32>>& PatternsAndOrders,
		TFunctionRef<void(UPCGVoxelApplyPatternsSettings&)> Edit)
	{
		UPCGVoxelApplyPatternsSettings* Settings = NewObject<UPCGVoxelApplyPatternsSettings>();
		Edit(*Settings);
		TArray<TPair<FName, const UPCGData*>> Inputs;
		Inputs.Add({ PCGPinConstants::DefaultInputLabel, In });
		for (const TPair<FPCGVoxelPattern, int32>& Entry : PatternsAndOrders)
		{
			UPCGVoxelPatternData* Data = NewObject<UPCGVoxelPatternData>();
			Data->Pattern = Entry.Key;
			Data->Order = Entry.Value;
			Inputs.Add({ PCGVoxelPatternPins::PatternsLabel, Data });
		}
		FNodeResult Result;
		for (const FPCGTaggedData& Output : Run<FPCGVoxelApplyPatternsElement>(Settings, Inputs))
		{
			if (Output.Pin == TEXT("Unmatched"))
			{
				Result.Unmatched = Cast<UPCGBasePointData>(Output.Data);
			}
			else
			{
				Result.Out = Cast<UPCGBasePointData>(Output.Data);
				Result.bOutKeptTag = Output.Tags.Contains(TEXT("KeptTag"));
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPatternsLayoutTest,
	"PCGUtils.Voxel.Patterns.AuthoredLayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelPatternsLayoutTest::RunTest(const FString&)
{
	using namespace PCGVoxelPatternsTests;
	const FPCGVoxelGrid Patch = MakeGrid(Block(3, 3, 1));

	// A 3 x 3 patch as three strips; swapping the strip axis changes the grouping and nothing else.
	for (const FIntVector Span : { FIntVector(3, 1, 1), FIntVector(1, 3, 1) })
	{
		const PCGVoxelPatterns::FResult Result = ApplyTo(Patch, { Strip(Span) });
		bool bNoOverlap = false;
		const TSet<FIntVector> Cells = Covered(Result, bNoOverlap);
		TestEqual(TEXT("Three strips"), Result.Chunks.Num(), 3);
		TestTrue(TEXT("Strips do not overlap"), bNoOverlap);
		TestEqual(TEXT("Strips cover all nine cells"), Cells.Num(), 9);
		for (const PCGVoxelPatterns::FChunk& Chunk : Result.Chunks)
		{
			TestEqual(TEXT("Strip span is the authored one"), Chunk.Span, Span);
			TestEqual(TEXT("Every strip has the same turn"), Chunk.MeshToGrid.X, Result.Chunks[0].MeshToGrid.X);
		}
	}

	// An exact box never shrinks: a 3 x 1 strip on a 2 x 2 patch makes nothing and takes nothing.
	{
		const FPCGVoxelGrid Small = MakeGrid(Block(2, 2, 1));
		const PCGVoxelPatterns::FResult Result = ApplyTo(Small, { Strip(FIntVector(3, 1, 1)) });
		TestEqual(TEXT("No strip fits"), Result.Chunks.Num(), 0);
		TestEqual(TEXT("Nothing is claimed"), Result.Claimed.CountSetBits(), 0);
		TArray<FPCGVoxelCellOut> Rest;
		PCGVoxelPatterns::Remainder(Small, Result, Rest);
		TestEqual(TEXT("Every cell is left over"), Rest.Num(), 4);
	}

	// Several boxes are all or nothing: an L of two boxes on a single row never takes its first box alone.
	{
		FPCGVoxelPattern TwoBoxes = Strip(FIntVector(2, 1, 1));
		FPCGVoxelPatternBox& Second = TwoBoxes.Boxes.AddDefaulted_GetRef();
		Second.First = FIntVector(0, 1, 0);
		Second.MeshIndex = 5;
		const PCGVoxelPatterns::FResult Row = ApplyTo(MakeGrid(Block(2, 1, 1)), { TwoBoxes });
		TestEqual(TEXT("Half a layout makes nothing"), Row.Chunks.Num(), 0);
		TestEqual(TEXT("Half a layout claims nothing"), Row.Claimed.CountSetBits(), 0);

		const PCGVoxelPatterns::FResult Whole = ApplyTo(MakeGrid({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) }), { TwoBoxes });
		TestEqual(TEXT("The whole layout makes both boxes"), Whole.Chunks.Num(), 2);
		TestEqual(TEXT("One fit"), Whole.NumFits[0], 1);
		TestTrue(TEXT("Each box keeps its mesh index"), Whole.Chunks.ContainsByPredicate([](const PCGVoxelPatterns::FChunk& C) { return C.MeshIndex == 5 && C.First == FIntVector(0, 1, 0); }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPatternsOrderTest,
	"PCGUtils.Voxel.Patterns.OrderAndClaiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelPatternsOrderTest::RunTest(const FString&)
{
	using namespace PCGVoxelPatternsTests;
	using namespace PCGVoxelPatterns;

	// A 4 x 4 slab: the 2 x 2 corner pattern takes all four corners, leaving nothing for the 1 x 1 corner pattern.
	const TArray<FIntVector> SlabCells = Block(4, 4, 1);
	{
		const FResult Result = ApplyTo(MakeGrid(SlabCells), { Presets::UprightCorner(2), Presets::UprightCorner(1) });
		bool bNoOverlap = false;
		const TSet<FIntVector> Cells = Covered(Result, bNoOverlap);
		TestEqual(TEXT("Four 2 x 2 corners"), CountOf(Result, 0), 4);
		TestEqual(TEXT("No 1 x 1 corner left"), CountOf(Result, 1), 0);
		TestTrue(TEXT("No cell is in two chunks"), bNoOverlap);
		TestEqual(TEXT("All sixteen cells are in a chunk"), Cells.Num(), 16);
		// Each corner chunk's two open sides are where its mesh's +X and +Z point.
		for (const FChunk& Chunk : Result.Chunks)
		{
			TestEqual(TEXT("Corner span"), Chunk.Span, FIntVector(2, 2, 1));
			for (const FIntVector Open : { Chunk.MeshToGrid.X, Chunk.MeshToGrid.Z })
			{
				const FIntVector Probe = Chunk.First + FIntVector(Open.X > 0 ? 2 : (Open.X < 0 ? -1 : 0), Open.Y > 0 ? 2 : (Open.Y < 0 ? -1 : 0), 0);
				TestFalse(TEXT("The mesh's open sides face empty space"), SlabCells.Contains(Probe));
				TestEqual(TEXT("Open sides are level"), Open.Z, 0);
			}
		}

		// The same cells in another point order give the same chunks.
		TArray<FIntVector> Reversed = SlabCells;
		Algo::Reverse(Reversed);
		const FResult Again = ApplyTo(MakeGrid(Reversed), { Presets::UprightCorner(2), Presets::UprightCorner(1) });
		TestEqual(TEXT("Point order does not change the count"), Again.Chunks.Num(), Result.Chunks.Num());
		for (int32 I = 0; I < FMath::Min(Again.Chunks.Num(), Result.Chunks.Num()); ++I)
		{
			TestEqual(TEXT("Point order does not change the chunks"), Again.Chunks[I].First, Result.Chunks[I].First);
			TestEqual(TEXT("Point order does not change the turns"), Again.Chunks[I].MeshToGrid.X, Result.Chunks[I].MeshToGrid.X);
		}
	}

	// The other way round, the 1 x 1 pattern takes the corner cells and no 2 x 2 fits any more.
	{
		const FResult Result = ApplyTo(MakeGrid(SlabCells), { Presets::UprightCorner(1), Presets::UprightCorner(2) });
		TestEqual(TEXT("Four 1 x 1 corners"), CountOf(Result, 0), 4);
		TestEqual(TEXT("No 2 x 2 corner left"), CountOf(Result, 1), 0);
	}

	// A claimed cell still reads as filled: B needs a filled +X neighbour, which A already took.
	{
		FPCGVoxelPattern A = Strip(FIntVector(1));
		A.Probes.Add({ FIntVector(-1, 0, 0), EPCGVoxelProbeState::Filled });
		FPCGVoxelPattern B = Strip(FIntVector(1));
		B.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Filled });
		const FResult Result = ApplyTo(MakeGrid(Block(2, 1, 1)), { A, B });
		TestEqual(TEXT("A takes the +X cell"), CountOf(Result, 0), 1);
		TestEqual(TEXT("B still sees it filled and takes the other"), CountOf(Result, 1), 1);
	}

	// Unclaimed: a cell an earlier pattern took reads as filled but not as unclaimed; the pattern's own fits do not count.
	{
		FPCGVoxelPattern TakesEnd = Strip(FIntVector(1));
		TakesEnd.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
		FPCGVoxelPattern NeedsPlain = Strip(FIntVector(1));
		NeedsPlain.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Unclaimed });
		const FPCGVoxelGrid Row = MakeGrid(Block(3, 1, 1));
		TestEqual(TEXT("Alone, cells 0 and 1 fit (its own claims do not block it)"), ApplyTo(Row, { NeedsPlain }).Chunks.Num(), 2);
		const FResult Result = ApplyTo(Row, { TakesEnd, NeedsPlain });
		TestEqual(TEXT("After the end cell is taken, only cell 0 has a plain neighbour"), CountOf(Result, 1), 1);
		TestTrue(TEXT("That fit is on cell 0"), Result.Chunks.ContainsByPredicate([](const FChunk& C) { return C.Pattern == 1 && C.First.X == 0; }));

		// Count: one probe covering a row of cells.
		FPCGVoxelPattern NeedsTwo = Strip(FIntVector(1));
		NeedsTwo.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Unclaimed, 2, EPCGVoxelPatternDirection::PosX });
		TestEqual(TEXT("Only cell 0 has two cells after it"), ApplyTo(Row, { NeedsTwo }).Chunks.Num(), 1);
	}

	// Corners need wall: a lone block and a wall's end are not corners unless the pattern asks for no wall.
	{
		TestEqual(TEXT("A lone 2 x 2 block is not a corner"), ApplyTo(MakeGrid(Block(2, 2, 1)), { Presets::UprightCorner(2) }).Chunks.Num(), 0);
		TestEqual(TEXT("With no wall asked for, it is"), ApplyTo(MakeGrid(Block(2, 2, 1)), { Presets::UprightCorner(2, 0) }).Chunks.Num(), 1);
		TestEqual(TEXT("The end of a wall two thick is not a 2 x 2 corner"), ApplyTo(MakeGrid(Block(4, 2, 1)), { Presets::UprightCorner(2) }).Chunks.Num(), 0);

		// Two walls two cells thick meeting in an L: one 2 x 2 corner, at the outside of the bend.
		TArray<FIntVector> Bend;
		for (int32 Y = 0; Y < 6; ++Y)
		{
			for (int32 X = 0; X < 6; ++X)
			{
				if (X >= 4 || Y >= 4)
				{
					Bend.Add(FIntVector(X, Y, 0));
				}
			}
		}
		const FPCGVoxelGrid BendGrid = MakeGrid(Bend);
		const FResult Corner = ApplyTo(BendGrid, { Presets::UprightCorner(2) });
		TestEqual(TEXT("One 2 x 2 corner on the bend"), Corner.Chunks.Num(), 1);
		if (Corner.Chunks.Num() == 1)
		{
			TestEqual(TEXT("At the outside of the bend"), Corner.Chunks[0].First, FIntVector(4, 4, 0));
		}

		// The bend's inside corner is filled, also once the 2 x 2 corner took the bend cell: the walls are still whole.
		TestEqual(TEXT("Fill on a plain bend"), ApplyTo(BendGrid, { Presets::InsideCornerFill() }).Chunks.Num(), 1);
		TestEqual(TEXT("Fill behind the corner chunk"), CountOf(ApplyTo(BendGrid, { Presets::UprightCorner(2), Presets::InsideCornerFill() }), 1), 1);

		// A fill rests on whole sides only: a wall cell whose chunk is cut on the side facing the fill does not hold it.
		const FPCGVoxelGrid Ell = MakeGrid({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) });
		FPCGVoxelPattern TakesWall = Strip(FIntVector(1));
		TakesWall.Probes.Add({ FIntVector(-1, 0, 0), EPCGVoxelProbeState::Filled });
		TakesWall.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
		TakesWall.Boxes[0].OpenSides = 1 << static_cast<int32>(EPCGVoxelPatternDirection::PosY);
		TestEqual(TEXT("No fill against a cut side"), CountOf(ApplyTo(Ell, { TakesWall, Presets::InsideCornerFill() }), 1), 0);
		TakesWall.Boxes[0].OpenSides = 1 << static_cast<int32>(EPCGVoxelPatternDirection::PosX);
		TestEqual(TEXT("Fill against the chunk's whole side"), CountOf(ApplyTo(Ell, { TakesWall, Presets::InsideCornerFill() }), 1), 1);
	}

	// Limits: a cell that may not be edited is never claimed, but probes still read it.
	{
		FPCGVoxelPattern NeedsNeighbour = Strip(FIntVector(1));
		NeedsNeighbour.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Filled });
		FLimits Limits;
		Limits.CanEdit = [](int32, const FIntVector& Cell) { return Cell.X != 2; };
		TestEqual(TEXT("Without limits cells 0, 1 and 2 are taken"), ApplyTo(MakeGrid(Block(4, 1, 1)), { NeedsNeighbour }).Chunks.Num(), 3);
		const FResult Result = ApplyTo(MakeGrid(Block(4, 1, 1)), { NeedsNeighbour }, Limits);
		TestEqual(TEXT("Cells 0 and 1 are taken (1 reads protected cell 2 as filled)"), Result.Chunks.Num(), 2);
		TestFalse(TEXT("The protected cell is not taken"), Result.Chunks.ContainsByPredicate([](const FChunk& C) { return C.First.X == 2; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPatternsJoinAddTest,
	"PCGUtils.Voxel.Patterns.JoinAndAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelPatternsJoinAddTest::RunTest(const FString&)
{
	using namespace PCGVoxelPatternsTests;
	using namespace PCGVoxelPatterns;

	// A wall one cell thick along Y, four long: the top edge joins into one chunk; Max Joined Span cuts it.
	{
		const FPCGVoxelGrid Wall = MakeGrid(Block(1, 4, 1));
		FPCGVoxelPattern Edge = Presets::TopEdge();
		const FResult Joined = ApplyTo(Wall, { Edge });
		TestEqual(TEXT("One joined edge"), Joined.Chunks.Num(), 1);
		TestEqual(TEXT("Four fits"), Joined.NumFits[0], 4);
		if (Joined.Chunks.Num() == 1)
		{
			TestEqual(TEXT("Joined span"), Joined.Chunks[0].Span, FIntVector(1, 4, 1));
			TestEqual(TEXT("The edge's open side is +X"), Joined.Chunks[0].MeshToGrid.X, FIntVector(1, 0, 0));
		}
		Edge.MaxJoinedSpan = 2;
		TestEqual(TEXT("Max Joined Span 2 gives two chunks"), ApplyTo(Wall, { Edge }).Chunks.Num(), 2);
		Edge.JoinAlong = 0;
		TestEqual(TEXT("No joining gives one chunk per cell"), ApplyTo(Wall, { Edge }).Chunks.Num(), 4);
	}

	// A slab's +Y side runs along X: the same pattern turns, and joins along the grid axis its Y landed on. The end
	// cell (3,1) is a corner; it fits as drawn first (+X open), so it is not part of the run.
	{
		const FResult Result = ApplyTo(MakeGrid(Block(4, 2, 1)), { Presets::TopEdge() });
		const FChunk* Run = Result.Chunks.FindByPredicate([](const FChunk& C) { return C.First == FIntVector(0, 1, 0); });
		if (TestNotNull(TEXT("A run starts at (0,1,0)"), Run))
		{
			TestEqual(TEXT("Span stays in grid axes"), Run->Span, FIntVector(3, 1, 1));
			TestEqual(TEXT("The mesh's long axis lies along grid X"), FMath::Abs(Run->MeshToGrid.Y.X), 1);
			TestEqual(TEXT("The mesh's open side faces +Y"), Run->MeshToGrid.X, FIntVector(0, 1, 0));
		}
	}

	// An L of three cells, two layers tall: the inside corner gets one added chunk, joined bottom to top.
	{
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < 2; ++Z)
		{
			Cells.Append({ FIntVector(0, 0, Z), FIntVector(1, 0, Z), FIntVector(0, 1, Z) });
		}
		const FPCGVoxelGrid Grid = MakeGrid(Cells);
		const FResult Result = ApplyTo(Grid, { Presets::InsideCornerFill() });
		TestEqual(TEXT("One added chunk"), Result.Chunks.Num(), 1);
		TestEqual(TEXT("No input cell is claimed"), Result.Claimed.CountSetBits(), 0);
		TestEqual(TEXT("Two cells added"), Result.Added.Num(), 2);
		if (Result.Chunks.Num() == 1)
		{
			const FChunk& Chunk = Result.Chunks[0];
			TestTrue(TEXT("Marked added"), Chunk.bAdded);
			TestEqual(TEXT("In the empty corner"), Chunk.First, FIntVector(1, 1, 0));
			TestEqual(TEXT("Both layers"), Chunk.Span, FIntVector(1, 1, 2));
			TestEqual(TEXT("Copies the cell its pattern started on"), Chunk.SourcePoint, 0);
			// The mesh's whole faces (-X and -Z) rest on the two filled cells beside the corner.
			const TSet<FIntVector> Backs = { Chunk.MeshToGrid.Apply(FIntVector(-1, 0, 0)), Chunk.MeshToGrid.Apply(FIntVector(0, 0, -1)) };
			TestTrue(TEXT("Whole faces against the filled sides"), Backs.Contains(FIntVector(-1, 0, 0)) && Backs.Contains(FIntVector(0, -1, 0)));
		}
		TArray<FPCGVoxelCellOut> Rest;
		Remainder(Grid, Result, Rest);
		TestEqual(TEXT("Supports are left whole"), Rest.Num(), 6);

		// Added cells show to a later pattern only when it asks.
		FPCGVoxelPattern NextToFill = Strip(FIntVector(1));
		NextToFill.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Filled });
		NextToFill.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
		TestEqual(TEXT("Not seen by default"), CountOf(ApplyTo(Grid, { Presets::InsideCornerFill(), NextToFill }), 1), 0);
		NextToFill.bProbesSeeAddedCells = true;
		TestEqual(TEXT("Seen when asked: cell (1,0) on both layers"), CountOf(ApplyTo(Grid, { Presets::InsideCornerFill(), NextToFill }), 1), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPatternsValidateTest,
	"PCGUtils.Voxel.Patterns.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelPatternsValidateTest::RunTest(const FString&)
{
	using namespace PCGVoxelPatternsTests;
	using namespace PCGVoxelPatterns;
	FText Error;

	TestTrue(TEXT("Presets are valid"), Validate(Presets::TopEdge(), Error) && Validate(Presets::UprightCorner(3), Error) && Validate(Presets::InsideCornerFill(), Error));

	auto Fails = [&](const TCHAR* What, const FPCGVoxelPattern& Pattern, const TCHAR* Needle)
	{
		FText Message;
		const bool bFailed = !Validate(Pattern, Message);
		TestTrue(*FString::Printf(TEXT("%s is rejected"), What), bFailed);
		TestTrue(*FString::Printf(TEXT("%s: the error names the invariant (got '%s')"), What, *Message.ToString()), Message.ToString().Contains(Needle));
	};

	FPCGVoxelPattern Pattern;
	Pattern.Boxes.Reset();
	Fails(TEXT("No boxes"), Pattern, TEXT("Boxes is empty"));

	Pattern = FPCGVoxelPattern();
	Pattern.Boxes[0].Span = FIntVector(1, 0, 1);
	Fails(TEXT("Zero span"), Pattern, TEXT("Span Y is 0"));

	Pattern = FPCGVoxelPattern();
	Pattern.Boxes[0].MeshUp = EPCGVoxelPatternDirection::NegY;
	Fails(TEXT("Mesh axes on one line"), Pattern, TEXT("Mesh Forward (+Y) and Mesh Up (-Y)"));

	Pattern = FPCGVoxelPattern();
	Pattern.Boxes[0].Span = FIntVector(2, 1, 1);
	Pattern.Boxes.AddDefaulted_GetRef().First = FIntVector(1, 0, 0);
	Fails(TEXT("Overlapping boxes"), Pattern, TEXT("both cover cell (1, 0, 0)"));

	Pattern = FPCGVoxelPattern();
	Pattern.Probes.Add({ FIntVector(0, 0, 0), EPCGVoxelProbeState::Empty });
	Fails(TEXT("Empty origin"), Pattern, TEXT("origin"));

	Pattern = FPCGVoxelPattern();
	Pattern.Operation = EPCGVoxelPatternOperation::Add;
	Fails(TEXT("Add on the origin"), Pattern, TEXT("covers the origin cell"));

	Pattern = FPCGVoxelPattern();
	Pattern.Boxes[0].Span = FIntVector(2, 1, 1);
	Pattern.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Empty });
	Fails(TEXT("Empty probe in a claimed box"), Pattern, TEXT("Probe 0 wants cell (1, 0, 0) empty"));

	Pattern = FPCGVoxelPattern();
	Pattern.Probes.Add({ FIntVector(0, 1, 0), EPCGVoxelProbeState::Empty });
	Pattern.Probes.Add({ FIntVector(-1, 1, 0), EPCGVoxelProbeState::Unclaimed, 3, EPCGVoxelPatternDirection::PosX });
	Fails(TEXT("Clashing probes"), Pattern, TEXT("Probe 0 and probe 1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPatternsNodeTest,
	"PCGUtils.Voxel.Patterns.ApplyNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelPatternsNodeTest::RunTest(const FString&)
{
	using namespace PCGVoxelPatternsTests;

	// A column of three cells along Y plus one loose cell. A 3 x 1 x 1 strip fits the column only when turned.
	TArray<FIntVector> Cells = Block(1, 3, 1);
	Cells.Add(FIntVector(5, 0, 0));
	FPCGVoxelPattern Turning = Strip(FIntVector(3, 1, 1));
	Turning.Turns = EPCGVoxelRuleTurns::AboutUp;
	Turning.Name = TEXT("Strip");
	Turning.DebugColor = FLinearColor(0.25f, 0.5f, 0.75f);
	Turning.Boxes[0].MeshIndex = 7;

	{
		const FNodeResult Result = RunNode(MakeVoxels(Cells), { { Turning, 0 } }, [](UPCGVoxelApplyPatternsSettings& S)
		{
			S.bWritePatternName = true;
			S.bWritePatternColor = true;
			S.bWriteIsAdded = true;
		});
		if (!TestNotNull(TEXT("Out"), Result.Out) || !TestNotNull(TEXT("Unmatched"), Result.Unmatched))
		{
			return false;
		}
		TestTrue(TEXT("Tags carry over"), Result.bOutKeptTag);
		TestEqual(TEXT("One chunk"), Result.Out->GetNumPoints(), 1);
		TestEqual(TEXT("The loose cell is unmatched"), Result.Unmatched->GetNumPoints(), 1);
		if (Result.Out->GetNumPoints() == 1)
		{
			const FTransform Transform = Result.Out->GetConstTransformValueRange()[0];
			const FVector Min = Result.Out->GetConstBoundsMinValueRange()[0];
			const FVector Max = Result.Out->GetConstBoundsMaxValueRange()[0];
			TestTrue(TEXT("At the column's centre"), Transform.GetLocation().Equals(FVector(0.0, 100.0, 0.0), 0.01));
			TestTrue(TEXT("Scale 1"), Transform.GetScale3D().Equals(FVector::OneVector));
			// The pattern's X (the strip's length) lies along grid Y.
			TestTrue(TEXT("Turned a quarter"), FMath::IsNearlyEqual(FMath::Abs(Transform.GetRotation().RotateVector(FVector::XAxisVector).Y), 1.0, 0.001));
			TestTrue(TEXT("Bounds are in the turned frame: long along local X"), Max.Equals(FVector(150.0, 50.0, 50.0), 0.01) && Min.Equals(FVector(-150.0, -50.0, -50.0), 0.01));
			TestTrue(TEXT("Span stays in grid axes"), ValueAt<FVector>(Result.Out, TEXT("VoxelSpan"), 0, FVector::ZeroVector).Equals(FVector(1.0, 3.0, 1.0)));
			TestEqual(TEXT("Mesh Index"), ValueAt<int32>(Result.Out, TEXT("MeshIndex"), 0, -99), 7);
			TestEqual(TEXT("Pattern Index"), ValueAt<int32>(Result.Out, TEXT("PatternIndex"), 0, -99), 0);
			TestEqual(TEXT("Pattern Name"), ValueAt<FName>(Result.Out, TEXT("PatternName"), 0, NAME_None), FName(TEXT("Strip")));
			TestTrue(TEXT("Pattern Color"), ValueAt<FVector4>(Result.Out, TEXT("PatternColor"), 0, FVector4(0, 0, 0, 0)).Equals(FVector4(0.25, 0.5, 0.75, 1.0), 0.001));
			TestFalse(TEXT("Is Added"), ValueAt<bool>(Result.Out, TEXT("IsAdded"), 0, true));
			TestTrue(TEXT("Point colour is the debug colour"), Result.Out->GetConstColorValueRange()[0].Equals(FVector4(0.25, 0.5, 0.75, 1.0), 0.001));
			TestEqual(TEXT("Source attributes carry over (first cell's Id)"), ValueAt<int32>(Result.Out, TEXT("Id"), 0, -99), 0);
		}
		if (Result.Unmatched->GetNumPoints() == 1)
		{
			TestEqual(TEXT("Unmatched Mesh Index"), ValueAt<int32>(Result.Unmatched, TEXT("MeshIndex"), 0, -99), -1);
			TestEqual(TEXT("Unmatched keeps its Id"), ValueAt<int32>(Result.Unmatched, TEXT("Id"), 0, -99), 3);
		}
	}

	// Keep: unmatched cells follow the chunks in Out with -1. Order decides who runs first, not connection order.
	{
		FPCGVoxelPattern Single = Strip(FIntVector(1));
		Single.Boxes[0].MeshIndex = 2;
		const FNodeResult Result = RunNode(MakeVoxels(Cells), { { Single, 5 }, { Turning, 1 } }, [](UPCGVoxelApplyPatternsSettings& S)
		{
			S.Unmatched = EPCGVoxelUnmatchedCells::Keep;
			S.bSetPointColor = false;
		});
		if (!TestNotNull(TEXT("Out (Keep)"), Result.Out))
		{
			return false;
		}
		TestNull(TEXT("No Unmatched data when kept"), Result.Unmatched);
		TestEqual(TEXT("The strip (Order 1) ran first, the single took the loose cell"), Result.Out->GetNumPoints(), 2);
		if (Result.Out->GetNumPoints() == 2)
		{
			TestEqual(TEXT("First chunk is the strip"), ValueAt<int32>(Result.Out, TEXT("MeshIndex"), 0, -99), 7);
			TestEqual(TEXT("Second is the single"), ValueAt<int32>(Result.Out, TEXT("MeshIndex"), 1, -99), 2);
			TestEqual(TEXT("Pattern Index follows run order"), ValueAt<int32>(Result.Out, TEXT("PatternIndex"), 1, -99), 1);
		}
	}

	// A chunked input: an untouched chunk stays a chunk on Unmatched; a chunk that lost cells gives the rest one by one.
	{
		const TArray<FIntVector> Firsts = { FIntVector(0, 0, 0), FIntVector(0, 0, 3) };
		const TArray<FIntVector> Spans = { FIntVector(2, 1, 1), FIntVector(3, 1, 1) };
		FPCGVoxelPattern OnlyAtOrigin = Strip(FIntVector(1));
		OnlyAtOrigin.Probes.Add({ FIntVector(1, 0, 0), EPCGVoxelProbeState::Filled });
		OnlyAtOrigin.Probes.Add({ FIntVector(2, 0, 0), EPCGVoxelProbeState::Empty });
		OnlyAtOrigin.Probes.Add({ FIntVector(-1, 0, 0), EPCGVoxelProbeState::Empty });
		const FNodeResult Result = RunNode(MakeVoxels(Firsts, 100.0, &Spans), { { OnlyAtOrigin, 0 } }, [](UPCGVoxelApplyPatternsSettings&) {});
		if (!TestNotNull(TEXT("Out (chunked)"), Result.Out) || !TestNotNull(TEXT("Unmatched (chunked)"), Result.Unmatched))
		{
			return false;
		}
		TestEqual(TEXT("One cell taken from the 2-cell chunk"), Result.Out->GetNumPoints(), 1);
		TestEqual(TEXT("Its other cell, and the whole 3-cell chunk"), Result.Unmatched->GetNumPoints(), 2);
		bool bWholeChunkKept = false;
		for (int32 I = 0; I < Result.Unmatched->GetNumPoints(); ++I)
		{
			bWholeChunkKept |= ValueAt<FVector>(Result.Unmatched, TEXT("VoxelSpan"), I, FVector::ZeroVector).Equals(FVector(3.0, 1.0, 1.0));
		}
		TestTrue(TEXT("The untouched chunk keeps its span"), bWholeChunkKept);
	}
	return true;
}

#endif
