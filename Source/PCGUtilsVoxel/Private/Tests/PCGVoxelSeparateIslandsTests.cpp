// Copyright Max Harris
//
// Voxel | Separate Islands: one set per island, connectivity, chunks, Min Cells, re-anchored grids as pivots.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelSeparateIslands.h"
#include "Elements/PCGVoxelTransformGrid.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelIslands.h"

namespace PCGVoxelSeparateIslandsTests
{
	using namespace PCGVoxelTestHelpers;

	const UPCGBasePointData* IslandPointsOf(const TArray<FPCGTaggedData>& Outputs, int32 Index)
	{
		return Outputs.IsValidIndex(Index) ? Cast<UPCGBasePointData>(Outputs[Index].Data) : nullptr;
	}

	TArray<FPCGTaggedData> Separate(UPCGVoxelSeparateIslandsSettings* Settings, const TArray<const UPCGData*>& Sets)
	{
		TArray<TPair<FName, const UPCGData*>> Inputs;
		for (const UPCGData* Set : Sets)
		{
			Inputs.Emplace(PCGPinConstants::DefaultInputLabel, Set);
		}
		return Run<FPCGVoxelSeparateIslandsElement>(Settings, Inputs);
	}

	template <typename T>
	bool ReadIslandData(const UPCGBasePointData* Points, FName Name, T& OutValue)
	{
		const FPCGMetadataDomain* Domain = Points ? Points->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttribute<T>* Attribute = Domain ? Domain->GetConstTypedAttribute<T>(Name) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}

	TArray<int32> IdsOf(const UPCGBasePointData* Points)
	{
		TArray<int32> Ids;
		const FPCGMetadataAttribute<int32>* Id = Points ? Points->Metadata->GetConstTypedAttribute<int32>(TEXT("Id")) : nullptr;
		if (Id)
		{
			for (const PCGMetadataEntryKey Key : Points->GetConstMetadataEntryValueRange())
			{
				Ids.Add(Id->GetValueFromItemKey(Key));
			}
		}
		return Ids;
	}

	bool ReadsAsSet(const UPCGBasePointData* Points, FPCGVoxelGrid& OutGrid)
	{
		FPCGContext Context;
		FPCGVoxelGridReadSettings Read;
		Read.OffGridPoints = EPCGVoxelOffGridPoints::Error;
		return Points && FPCGVoxelGrid::Build(&Context, Points, Read, FText::GetEmpty(), 0, OutGrid);
	}

	/** Every cell's world centre, rounded to 0.1 and sorted. */
	TArray<FIntVector> WorldCells(const FPCGVoxelGrid& Grid)
	{
		TArray<FIntVector> Out;
		for (const FIntVector& Cell : Grid.Cells)
		{
			const FVector P = Grid.CellCenterWorld(Cell);
			Out.Add(FIntVector(FMath::RoundToInt(P.X * 10.0), FMath::RoundToInt(P.Y * 10.0), FMath::RoundToInt(P.Z * 10.0)));
		}
		Out.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSeparateIslandsSplitTest,
	"PCGUtils.Voxel.SeparateIslands.OneSetPerIslandInCellOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSeparateIslandsSplitTest::RunTest(const FString&)
{
	using namespace PCGVoxelSeparateIslandsTests;
	// Two islands, points interleaved: a 2-cell row high up (z = 5) listed first, a 3-cell row at z = 0.
	const UPCGPointArrayData* In = MakeVoxels({
		FIntVector(0, 0, 5), FIntVector(10, 0, 0), FIntVector(1, 0, 5), FIntVector(11, 0, 0), FIntVector(12, 0, 0) });

	UPCGVoxelSeparateIslandsSettings* Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->bWriteIslandCount = true;
	Settings->bWriteCellCount = true;
	const TArray<FPCGTaggedData> Outputs = Separate(Settings, { In });
	UTEST_EQUAL(TEXT("Two islands, two sets"), Outputs.Num(), 2);

	// Cell order is Z first, so the low row is island 0.
	const UPCGBasePointData* Low = IslandPointsOf(Outputs, 0);
	const UPCGBasePointData* High = IslandPointsOf(Outputs, 1);
	TestEqual(TEXT("Island 0 is the low row, its points in input order"), IdsOf(Low), TArray<int32>({ 1, 3, 4 }));
	TestEqual(TEXT("Island 1 is the high row"), IdsOf(High), TArray<int32>({ 0, 2 }));

	int32 Value = -1;
	TestTrue(TEXT("Island 0 has IslandIndex 0"), ReadIslandData(Low, TEXT("IslandIndex"), Value) && Value == 0);
	TestTrue(TEXT("Island 1 has IslandIndex 1"), ReadIslandData(High, TEXT("IslandIndex"), Value) && Value == 1);
	TestTrue(TEXT("IslandCount is 2"), ReadIslandData(High, TEXT("IslandCount"), Value) && Value == 2);
	TestTrue(TEXT("Island 0 has 3 cells"), ReadIslandData(Low, TEXT("IslandCellCount"), Value) && Value == 3);
	TestTrue(TEXT("Tags kept"), Outputs[0].Tags.Contains(TEXT("KeptTag")));
	FVector Size;
	TestTrue(TEXT("Input @Data kept (VoxelSize)"), ReadIslandData(Low, TEXT("VoxelSize"), Size) && Size.Equals(FVector(100.0)));

	// Each island is a valid voxel set covering the same world cells it had, points not moved.
	FPCGVoxelGrid LowGrid;
	UTEST_TRUE(TEXT("Island 0 reads as a voxel set"), ReadsAsSet(Low, LowGrid));
	TestEqual(TEXT("Island 0 world cells"), WorldCells(LowGrid), TArray<FIntVector>({ FIntVector(10000, 0, 0), FIntVector(11000, 0, 0), FIntVector(12000, 0, 0) }));
	TestTrue(TEXT("Points do not move"), Low->GetConstTransformValueRange()[0].GetLocation().Equals(FVector(1000.0, 0.0, 0.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSeparateIslandsConnectivityTest,
	"PCGUtils.Voxel.SeparateIslands.ConnectivityAndMinCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSeparateIslandsConnectivityTest::RunTest(const FString&)
{
	using namespace PCGVoxelSeparateIslandsTests;
	// Cell 0 and 1 touch along an edge; 1 and 2 touch only at a corner.
	const UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 1, 0), FIntVector(2, 2, 1) });
	UPCGVoxelSeparateIslandsSettings* Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->Connectivity = EPCGVoxelConnectivity::Faces;
	TestEqual(TEXT("Faces: three islands"), Separate(Settings, { In }).Num(), 3);
	Settings->Connectivity = EPCGVoxelConnectivity::Edges;
	TestEqual(TEXT("Edges: two islands"), Separate(Settings, { In }).Num(), 2);
	Settings->Connectivity = EPCGVoxelConnectivity::Corners;
	TestEqual(TEXT("Corners: one island"), Separate(Settings, { In }).Num(), 1);

	// Min Cells drops the small islands and numbers the rest without gaps.
	const UPCGPointArrayData* Mixed = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(5, 0, 0), FIntVector(6, 0, 0), FIntVector(9, 0, 0), FIntVector(20, 0, 0), FIntVector(21, 0, 0) });
	Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->MinCells = 2;
	const TArray<FPCGTaggedData> Outputs = Separate(Settings, { Mixed });
	UTEST_EQUAL(TEXT("Two islands of 2 cells kept"), Outputs.Num(), 2);
	int32 Value = -1;
	TestTrue(TEXT("Second kept island is IslandIndex 1"), ReadIslandData(IslandPointsOf(Outputs, 1), TEXT("IslandIndex"), Value) && Value == 1);
	TestEqual(TEXT("Second kept island is the 20-21 pair"), IdsOf(IslandPointsOf(Outputs, 1)), TArray<int32>({ 4, 5 }));

	// The shared library numbers the same way Cell Properties' components always did.
	FPCGVoxelGrid Grid;
	UTEST_TRUE(TEXT("Mixed reads"), ReadsAsSet(Mixed, Grid));
	FPCGVoxelIslands Islands;
	PCGVoxel::FindIslands(Grid, EPCGVoxelConnectivity::Faces, Islands);
	TestEqual(TEXT("Library finds 4 islands"), Islands.Num(), 4);
	TestEqual(TEXT("Island bounds"), Islands.MaxCell[1], FIntVector(6, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSeparateIslandsChunkTest,
	"PCGUtils.Voxel.SeparateIslands.ChunksAndIgnoredPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSeparateIslandsChunkTest::RunTest(const FString&)
{
	using namespace PCGVoxelSeparateIslandsTests;
	// A 3 x 1 x 1 chunk touching a single cell at its +X end, and a lone 2 x 2 x 1 chunk elsewhere.
	const TArray<FIntVector> Spans = { FIntVector(3, 1, 1), FIntVector(1, 1, 1), FIntVector(2, 2, 1) };
	const UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(3, 0, 0), FIntVector(10, 10, 0) }, 100.0, &Spans);
	UPCGVoxelSeparateIslandsSettings* Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->bWriteCellCount = true;
	const TArray<FPCGTaggedData> Outputs = Separate(Settings, { In });
	UTEST_EQUAL(TEXT("Two islands"), Outputs.Num(), 2);
	TestEqual(TEXT("Chunk and cell together"), IdsOf(IslandPointsOf(Outputs, 0)), TArray<int32>({ 0, 1 }));
	int32 Cells = 0;
	TestTrue(TEXT("Island 0 counts 4 cells"), ReadIslandData(IslandPointsOf(Outputs, 0), TEXT("IslandCellCount"), Cells) && Cells == 4);
	TestTrue(TEXT("Island 1 counts 4 cells"), ReadIslandData(IslandPointsOf(Outputs, 1), TEXT("IslandCellCount"), Cells) && Cells == 4);
	const UPCGBasePointData* Chunked = IslandPointsOf(Outputs, 1);
	const FPCGMetadataAttribute<FVector>* Span = Chunked->Metadata->GetConstTypedAttribute<FVector>(TEXT("VoxelSpan"));
	TestTrue(TEXT("Chunks stay chunks"), Chunked->GetNumPoints() == 1 && Span && Span->GetValueFromItemKey(Chunked->GetConstMetadataEntryValueRange()[0]).Equals(FVector(2, 2, 1)));

	// A second point on an occupied cell (Keep First ignores it) goes with that cell's island.
	const UPCGPointArrayData* Dup = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(9, 0, 0), FIntVector(0, 0, 0) });
	const TArray<FPCGTaggedData> DupOut = Separate(NewObject<UPCGVoxelSeparateIslandsSettings>(), { Dup });
	UTEST_EQUAL(TEXT("Two islands"), DupOut.Num(), 2);
	TestEqual(TEXT("The ignored point stays with its cell's island"), IdsOf(IslandPointsOf(DupOut, 0)), TArray<int32>({ 0, 2 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSeparateIslandsAnchorTest,
	"PCGUtils.Voxel.SeparateIslands.ReanchoredGridIsThePivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSeparateIslandsAnchorTest::RunTest(const FString&)
{
	using namespace PCGVoxelSeparateIslandsTests;
	// A 3-cell row (x = 2..4) and a 2-cell row (x = 10..11), on a grid turned 30 degrees and moved.
	UPCGPointArrayData* In = MakeVoxels({ FIntVector(2, 0, 0), FIntVector(3, 0, 0), FIntVector(4, 0, 0), FIntVector(10, 0, 0), FIntVector(11, 0, 0) });
	const FTransform Parent(FRotator(0.0, 30.0, 0.0).Quaternion(), FVector(1000.0, -500.0, 50.0));
	{
		// Place the points on the turned grid.
		auto Transforms = In->GetTransformValueRange();
		for (int32 I = 0; I < In->GetNumPoints(); ++I)
		{
			Transforms[I] = FTransform(Parent.GetRotation(), Parent.TransformPositionNoScale(Transforms[I].GetLocation()));
		}
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(In), TEXT("VoxelGridTransform"), Parent);
	}

	UPCGVoxelSeparateIslandsSettings* Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->bWriteBoundsCenter = true;
	const TArray<FPCGTaggedData> Outputs = Separate(Settings, { In });
	UTEST_EQUAL(TEXT("Two islands"), Outputs.Num(), 2);

	FTransform Odd, Even;
	UTEST_TRUE(TEXT("Odd row has a grid transform"), ReadIslandData(IslandPointsOf(Outputs, 0), TEXT("VoxelGridTransform"), Odd));
	UTEST_TRUE(TEXT("Even row has a grid transform"), ReadIslandData(IslandPointsOf(Outputs, 1), TEXT("VoxelGridTransform"), Even));
	const FVector OddExpected = Parent.TransformPositionNoScale(FVector(300.0, 0.0, 0.0));
	TestTrue(*FString::Printf(TEXT("Odd row's origin is its centre cell %s (got %s)"), *OddExpected.ToString(), *Odd.GetLocation().ToString()),
		Odd.GetLocation().Equals(OddExpected, 1e-6));
	TestTrue(TEXT("The grid keeps its rotation"), Odd.GetRotation().Equals(Parent.GetRotation(), 1e-9));
	// Even width: the centre is at x = 10.5, so the origin is the max-side cell, x = 11.
	TestTrue(TEXT("Even row's origin rounds toward the max side"), Even.GetLocation().Equals(Parent.TransformPositionNoScale(FVector(1100.0, 0.0, 0.0)), 1e-6));
	FVector Center;
	TestTrue(TEXT("Even row's exact centre is written"), ReadIslandData(IslandPointsOf(Outputs, 1), TEXT("IslandCenter"), Center)
		&& Center.Equals(Parent.TransformPositionNoScale(FVector(1050.0, 0.0, 0.0)), 1e-6));

	// The re-anchored set reads with the same world cells.
	FPCGVoxelGrid OddGrid;
	UTEST_TRUE(TEXT("Odd row reads"), ReadsAsSet(IslandPointsOf(Outputs, 0), OddGrid));
	TestEqual(TEXT("Its cells are now numbered from its centre"), OddGrid.MinCell, FIntVector(-1, 0, 0));

	// Transform Grid about the grid origin (Location 0, Grid space): a half turn maps the odd row onto itself.
	UPCGVoxelTransformGridSettings* Turn = NewObject<UPCGVoxelTransformGridSettings>();
	Turn->Pivot = EPCGVoxelTransformPivot::Location;
	Turn->Rotation.Constant = FRotator(0.0, 180.0, 0.0);
	const TArray<FPCGTaggedData> Turned = Run<FPCGVoxelTransformGridElement>(Turn, { { PCGPinConstants::DefaultInputLabel, Outputs[0].Data } });
	FPCGVoxelGrid TurnedGrid;
	UTEST_TRUE(TEXT("Turned row reads"), ReadsAsSet(IslandPointsOf(Turned, 0), TurnedGrid));
	TestEqual(TEXT("A half turn about the island's own origin leaves it in place"), WorldCells(TurnedGrid), WorldCells(OddGrid));

	// Off: every island keeps the input's grid transform.
	Settings->bReanchorGrid = false;
	FTransform Kept;
	const TArray<FPCGTaggedData> Plain = Separate(Settings, { In });
	TestTrue(TEXT("Without re-anchoring the input grid is kept"),
		ReadIslandData(IslandPointsOf(Plain, 1), TEXT("VoxelGridTransform"), Kept) && Kept.GetLocation().Equals(Parent.GetLocation(), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSeparateIslandsNamesTest,
	"PCGUtils.Voxel.SeparateIslands.OutputNamesAreChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSeparateIslandsNamesTest::RunTest(const FString&)
{
	using namespace PCGVoxelSeparateIslandsTests;
	const UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(5, 0, 0) });

	AddExpectedError(TEXT("Island Index Attribute is empty"), EAutomationExpectedErrorFlags::Contains, 1, false);
	UPCGVoxelSeparateIslandsSettings* Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->IslandIndexAttribute = NAME_None;
	TestEqual(TEXT("An empty output name stops the node"), Separate(Settings, { In }).Num(), 0);

	AddExpectedError(TEXT("which is the grid's own Grid Transform attribute"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Settings = NewObject<UPCGVoxelSeparateIslandsSettings>();
	Settings->IslandIndexAttribute = TEXT("VoxelGridTransform");
	TestEqual(TEXT("An output named like the grid transform stops the node"), Separate(Settings, { In }).Num(), 0);
	return true;
}

#endif
