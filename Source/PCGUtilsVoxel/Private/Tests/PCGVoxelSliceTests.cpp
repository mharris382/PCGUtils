// Copyright Max Harris
//
// Voxel | Slice: slabs along an axis, indices and fractions, chunks cut at the boundaries, Surrounded, and the
// Separate Islands -> Slice -> Transform Grid twist chain.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelSeparateIslands.h"
#include "Elements/PCGVoxelSlice.h"
#include "Elements/PCGVoxelTransformGrid.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelSliceTests
{
	using namespace PCGVoxelTestHelpers;

	const UPCGBasePointData* SlicePointsOf(const TArray<FPCGTaggedData>& Outputs, int32 Index)
	{
		return Outputs.IsValidIndex(Index) ? Cast<UPCGBasePointData>(Outputs[Index].Data) : nullptr;
	}

	TArray<FPCGTaggedData> RunOn(UPCGSettings* Settings, const TArray<FPCGTaggedData>& Sets)
	{
		TArray<TPair<FName, const UPCGData*>> Inputs;
		for (const FPCGTaggedData& Set : Sets)
		{
			Inputs.Emplace(PCGPinConstants::DefaultInputLabel, Set.Data);
		}
		if (Cast<UPCGVoxelSliceSettings>(Settings))
		{
			return Run<FPCGVoxelSliceElement>(Settings, Inputs);
		}
		if (Cast<UPCGVoxelSeparateIslandsSettings>(Settings))
		{
			return Run<FPCGVoxelSeparateIslandsElement>(Settings, Inputs);
		}
		return Run<FPCGVoxelTransformGridElement>(Settings, Inputs);
	}

	TArray<FPCGTaggedData> SliceSets(UPCGVoxelSliceSettings* Settings, const TArray<const UPCGData*>& Sets)
	{
		TArray<TPair<FName, const UPCGData*>> Inputs;
		for (const UPCGData* Set : Sets)
		{
			Inputs.Emplace(PCGPinConstants::DefaultInputLabel, Set);
		}
		return Run<FPCGVoxelSliceElement>(Settings, Inputs);
	}

	template <typename T>
	bool ReadSliceData(const UPCGBasePointData* Points, FName Name, T& OutValue)
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

	template <typename T>
	TArray<T> PointValues(const UPCGBasePointData* Points, FName Name)
	{
		TArray<T> Out;
		const FPCGMetadataAttribute<T>* Attribute = Points ? Points->Metadata->GetConstTypedAttribute<T>(Name) : nullptr;
		if (Attribute)
		{
			for (const PCGMetadataEntryKey Key : Points->GetConstMetadataEntryValueRange())
			{
				Out.Add(Attribute->GetValueFromItemKey(Key));
			}
		}
		return Out;
	}

	bool ReadsStrict(const UPCGBasePointData* Points, FPCGVoxelGrid& OutGrid)
	{
		FPCGContext Context;
		FPCGVoxelGridReadSettings Read;
		Read.OffGridPoints = EPCGVoxelOffGridPoints::Error;
		Read.DuplicateCells = EPCGVoxelDuplicateCells::Error;
		return Points && FPCGVoxelGrid::Build(&Context, Points, Read, FText::GetEmpty(), 0, OutGrid);
	}

	TArray<FIntVector> WorldCellsOf(const FPCGVoxelGrid& Grid)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSliceFloorsTest,
	"PCGUtils.Voxel.Slice.FloorsIndicesAndFractions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSliceFloorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelSliceTests;
	// A 2 x 2 x 4 column standing on z = 3.
	TArray<FIntVector> Cells = Block(2, 2, 4);
	for (FIntVector& Cell : Cells)
	{
		Cell.Z += 3;
	}
	const UPCGPointArrayData* Column = MakeVoxels(Cells);
	UPCGVoxelSliceSettings* Settings = NewObject<UPCGVoxelSliceSettings>();
	Settings->bWriteSliceCount = true;
	Settings->bWriteSliceDistance = true;
	const TArray<FPCGTaggedData> Outputs = SliceSets(Settings, { Column });
	UTEST_EQUAL(TEXT("Four floors"), Outputs.Num(), 4);
	for (int32 Floor = 0; Floor < 4; ++Floor)
	{
		const UPCGBasePointData* Slice = SlicePointsOf(Outputs, Floor);
		int32 SliceIndex = -1, Count = 0;
		double Fraction = -1.0, Distance = 0.0;
		TestEqual(*FString::Printf(TEXT("Floor %d has 4 points"), Floor), Slice->GetNumPoints(), 4);
		TestTrue(*FString::Printf(TEXT("Floor %d index"), Floor), ReadSliceData(Slice, TEXT("SliceIndex"), SliceIndex) && SliceIndex == Floor);
		TestTrue(*FString::Printf(TEXT("Floor %d fraction"), Floor), ReadSliceData(Slice, TEXT("SliceFraction"), Fraction) && FMath::IsNearlyEqual(Fraction, Floor / 3.0));
		TestTrue(TEXT("Slice count 4"), ReadSliceData(Slice, TEXT("SliceCount"), Count) && Count == 4);
		TestTrue(*FString::Printf(TEXT("Floor %d distance (got %f)"), Floor, Distance), ReadSliceData(Slice, TEXT("SliceDistance"), Distance) && FMath::IsNearlyEqual(Distance, (3 + Floor) * 100.0));
		TestEqual(*FString::Printf(TEXT("Floor %d ids"), Floor), PointValues<int32>(Slice, TEXT("Id")), TArray<int32>({ Floor * 4, Floor * 4 + 1, Floor * 4 + 2, Floor * 4 + 3 }));
	}
	TestTrue(TEXT("Tags kept"), Outputs[0].Tags.Contains(TEXT("KeptTag")));

	// Thickness 2 from the grid origin: cells z = -3..2 make slices -2..1, the lowest holding z = -3 only.
	TArray<FIntVector> Low;
	for (int32 Z = -3; Z <= 2; ++Z)
	{
		Low.Add(FIntVector(0, 0, Z));
	}
	Settings = NewObject<UPCGVoxelSliceSettings>();
	Settings->Thickness = 2;
	Settings->Start = EPCGVoxelSliceStart::GridOrigin;
	Settings->bWriteSliceDistance = true;
	const TArray<FPCGTaggedData> Grid = SliceSets(Settings, { MakeVoxels(Low) });
	UTEST_EQUAL(TEXT("Four slices"), Grid.Num(), 4);
	int32 First = 0;
	double Distance = 0.0, Fraction = -1.0;
	TestTrue(TEXT("Lowest is slice -2"), ReadSliceData(SlicePointsOf(Grid, 0), TEXT("SliceIndex"), First) && First == -2);
	TestEqual(TEXT("Lowest holds one cell"), SlicePointsOf(Grid, 0)->GetNumPoints(), 1);
	TestTrue(TEXT("Lowest's middle is cell -3.5"), ReadSliceData(SlicePointsOf(Grid, 0), TEXT("SliceDistance"), Distance) && FMath::IsNearlyEqual(Distance, -350.0));
	TestTrue(TEXT("Highest fraction is 1"), ReadSliceData(SlicePointsOf(Grid, 3), TEXT("SliceFraction"), Fraction) && FMath::IsNearlyEqual(Fraction, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSliceChunksTest,
	"PCGUtils.Voxel.Slice.ChunksAreCutAtBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSliceChunksTest::RunTest(const FString&)
{
	using namespace PCGVoxelSliceTests;
	// One 1 x 1 x 5 chunk (z = 0..4) whose top sticks out 10 past its cells, beside a lone cell at z = 0.
	const TArray<FIntVector> Spans = { FIntVector(1, 1, 5), FIntVector(1, 1, 1) };
	UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) }, 100.0, &Spans);
	In->GetBoundsMaxValueRange()[0].Z = 260.0;

	UPCGVoxelSliceSettings* Settings = NewObject<UPCGVoxelSliceSettings>();
	Settings->Thickness = 2;
	const TArray<FPCGTaggedData> Outputs = SliceSets(Settings, { In });
	UTEST_EQUAL(TEXT("Three slices"), Outputs.Num(), 3);

	const double ExpectedZ[3] = { 50.0, 250.0, 400.0 };
	const double ExpectedMin[3] = { -100.0, -100.0, -50.0 };
	const double ExpectedMax[3] = { 100.0, 100.0, 60.0 };
	const double ExpectedSpan[3] = { 2.0, 2.0, 1.0 };
	for (int32 S = 0; S < 3; ++S)
	{
		const UPCGBasePointData* Slice = SlicePointsOf(Outputs, S);
		const int32 ChunkPiece = 0; // the chunk is point 0, so it comes first in every slice
		const FVector Location = Slice->GetConstTransformValueRange()[ChunkPiece].GetLocation();
		const FVector Min = Slice->GetConstBoundsMinValueRange()[ChunkPiece];
		const FVector Max = Slice->GetConstBoundsMaxValueRange()[ChunkPiece];
		TestTrue(*FString::Printf(TEXT("Slice %d piece at z %f (got %s)"), S, ExpectedZ[S], *Location.ToString()), Location.Equals(FVector(0.0, 0.0, ExpectedZ[S]), 1e-6));
		TestTrue(*FString::Printf(TEXT("Slice %d piece bounds z %f..%f (got %f..%f)"), S, ExpectedMin[S], ExpectedMax[S], Min.Z, Max.Z),
			FMath::IsNearlyEqual(Min.Z, ExpectedMin[S]) && FMath::IsNearlyEqual(Max.Z, ExpectedMax[S]));
		TestTrue(*FString::Printf(TEXT("Slice %d piece keeps its X bounds"), S), FMath::IsNearlyEqual(Min.X, -50.0) && FMath::IsNearlyEqual(Max.X, 50.0));
		const TArray<FVector> SpanValues = PointValues<FVector>(Slice, TEXT("VoxelSpan"));
		TestTrue(*FString::Printf(TEXT("Slice %d piece span"), S), SpanValues.Num() > 0 && FMath::IsNearlyEqual(SpanValues[0].Z, ExpectedSpan[S]));
		TestTrue(*FString::Printf(TEXT("Slice %d piece keeps its attributes"), S), PointValues<int32>(Slice, TEXT("Id")).Num() > 0 && PointValues<int32>(Slice, TEXT("Id"))[0] == 0);
		FPCGVoxelGrid Read;
		TestTrue(*FString::Printf(TEXT("Slice %d reads as a voxel set"), S), ReadsStrict(Slice, Read));
	}
	TestEqual(TEXT("The lone cell is in slice 0 only"), SlicePointsOf(Outputs, 0)->GetNumPoints(), 2);

	// A turned grid: each slice still sits exactly on the grid.
	UPCGVoxelTransformGridSettings* Turn = NewObject<UPCGVoxelTransformGridSettings>();
	Turn->Rotation.Constant = FRotator(35.0, 20.0, 70.0);
	const TArray<FPCGTaggedData> Turned = Run<FPCGVoxelTransformGridElement>(Turn, { { PCGPinConstants::DefaultInputLabel, In } });
	FPCGVoxelGrid Whole;
	UTEST_TRUE(TEXT("The turned set reads"), ReadsStrict(SlicePointsOf(Turned, 0), Whole));
	const TArray<FPCGTaggedData> TurnedSlices = RunOn(Settings, Turned);
	UTEST_EQUAL(TEXT("Three turned slices"), TurnedSlices.Num(), 3);
	TArray<FIntVector> All;
	for (int32 S = 0; S < 3; ++S)
	{
		FPCGVoxelGrid Read;
		UTEST_TRUE(*FString::Printf(TEXT("Turned slice %d reads strictly"), S), ReadsStrict(SlicePointsOf(TurnedSlices, S), Read));
		All.Append(WorldCellsOf(Read));
	}
	All.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
	TestEqual(TEXT("Turned slices cover the turned set's cells"), All, WorldCellsOf(Whole));
	const FVector TopMax = SlicePointsOf(TurnedSlices, 2)->GetConstBoundsMaxValueRange()[0];
	TestTrue(TEXT("The turned top piece keeps its 10 overhang"), FMath::IsNearlyEqual(TopMax.Z, 60.0, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSliceSurroundedTest,
	"PCGUtils.Voxel.Slice.SurroundedSeesCutFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSliceSurroundedTest::RunTest(const FString&)
{
	using namespace PCGVoxelSliceTests;
	// A 3 x 3 x 3 block whose centre cell (point 13) is surrounded.
	UPCGPointArrayData* In = MakeVoxels(Block(3, 3, 3));
	{
		FPCGMetadataDomain* Elements = In->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<bool>* Surrounded = Elements->CreateAttribute<bool>(TEXT("IsSurrounded"), false, false);
		Surrounded->SetValue(In->GetMetadataEntryValueRange()[13], true);
	}
	UPCGVoxelSliceSettings* Settings = NewObject<UPCGVoxelSliceSettings>();
	TArray<FPCGTaggedData> Outputs = SliceSets(Settings, { In });
	UTEST_EQUAL(TEXT("Three floors"), Outputs.Num(), 3);
	TestFalse(TEXT("Cut faces open: the centre shows its top and bottom"), PointValues<bool>(SlicePointsOf(Outputs, 1), TEXT("IsSurrounded"))[4]);

	Settings->bCutFacesAreOpen = false;
	Outputs = SliceSets(Settings, { In });
	TestTrue(TEXT("Cut faces hidden: the centre stays surrounded"), PointValues<bool>(SlicePointsOf(Outputs, 1), TEXT("IsSurrounded"))[4]);
	TestEqual(TEXT("Its neighbours stay not surrounded"), PointValues<bool>(SlicePointsOf(Outputs, 1), TEXT("IsSurrounded"))[0], false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSliceTwistChainTest,
	"PCGUtils.Voxel.Slice.SlicesOfAnIslandTurnAboutItsCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSliceTwistChainTest::RunTest(const FString&)
{
	using namespace PCGVoxelSliceTests;
	// A 3 x 3 x 2 column at x = 10..12 and a single cell far away: Separate Islands re-anchors the column at (11, 1, 1)
	// (two cells tall, so the centre rounds up).
	TArray<FIntVector> Cells;
	for (const FIntVector& Cell : Block(3, 3, 2))
	{
		Cells.Add(Cell + FIntVector(10, 0, 0));
	}
	Cells.Add(FIntVector(-20, 0, 0));
	const UPCGPointArrayData* In = MakeVoxels(Cells);
	const TArray<FPCGTaggedData> Separated = Run<FPCGVoxelSeparateIslandsElement>(NewObject<UPCGVoxelSeparateIslandsSettings>(),
		{ { PCGPinConstants::DefaultInputLabel, In } });
	UTEST_EQUAL(TEXT("Two islands"), Separated.Num(), 2);

	// Island 0 is the lone cell (x = -20 comes first in cell order on z = 0); slice the column.
	const TArray<FPCGTaggedData> Column = { Separated[1] };
	const TArray<FPCGTaggedData> Slices = RunOn(NewObject<UPCGVoxelSliceSettings>(), Column);
	UTEST_EQUAL(TEXT("Two slices"), Slices.Num(), 2);
	FTransform SliceGrid;
	TestTrue(TEXT("Slices keep the island's re-anchored grid"),
		ReadSliceData(SlicePointsOf(Slices, 1), TEXT("VoxelGridTransform"), SliceGrid) && SliceGrid.GetLocation().Equals(FVector(1100.0, 100.0, 100.0), 1e-6));

	// Each slice turned a different amount about the grid origin (a quarter and a half turn) stays on the column.
	UPCGVoxelTransformGridSettings* Turn = NewObject<UPCGVoxelTransformGridSettings>();
	Turn->Pivot = EPCGVoxelTransformPivot::Location;
	for (int32 S = 0; S < 2; ++S)
	{
		Turn->Rotation.Constant = FRotator(0.0, 90.0 * (S + 1), 0.0);
		const TArray<FPCGTaggedData> Turned = RunOn(Turn, { Slices[S] });
		FPCGVoxelGrid Before, After;
		UTEST_TRUE(TEXT("Slice reads"), ReadsStrict(SlicePointsOf(Slices, S), Before));
		UTEST_TRUE(TEXT("Turned slice reads"), ReadsStrict(SlicePointsOf(Turned, 0), After));
		TestEqual(*FString::Printf(TEXT("Slice %d turns in place about the column's centre"), S), WorldCellsOf(After), WorldCellsOf(Before));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSliceErrorsTest,
	"PCGUtils.Voxel.Slice.SettingsAreChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSliceErrorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelSliceTests;
	const UPCGPointArrayData* In = MakeVoxels(Block(1, 1, 3));

	AddExpectedError(TEXT("Thickness is 0; it must be at least 1 cell."), EAutomationExpectedErrorFlags::Contains, 1, false);
	UPCGVoxelSliceSettings* Settings = NewObject<UPCGVoxelSliceSettings>();
	Settings->Thickness = 0;
	TestEqual(TEXT("Thickness 0 stops the node"), SliceSets(Settings, { In }).Num(), 0);

	AddExpectedError(TEXT("Slice Index Attribute and Slice Fraction Attribute are both 'Same'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Settings = NewObject<UPCGVoxelSliceSettings>();
	Settings->SliceIndexAttribute = TEXT("Same");
	Settings->SliceFractionAttribute = TEXT("Same");
	TestEqual(TEXT("Shared names stop the node"), SliceSets(Settings, { In }).Num(), 0);
	return true;
}

#endif
