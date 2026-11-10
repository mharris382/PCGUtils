// Copyright Max Harris
//
// Voxel | Air Flow: flow goes around solids, wind shadows, sealed air, the floor, coarse air cells, errors.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelAirFlow.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelAirFlowTests
{
	using namespace PCGVoxelTestHelpers;

	/** Settings for exact, repeatable checks: no shadow, no blur, no turbulence, a tight solve. */
	UPCGVoxelAirFlowSettings* MakeSettings()
	{
		UPCGVoxelAirFlowSettings* Settings = NewObject<UPCGVoxelAirFlowSettings>();
		Settings->Wind.Constant = FVector(100.0, 0.0, 0.0);
		Settings->WakeLength = 0.0;
		Settings->SmoothPasses = 0;
		Settings->MaxIterations = 2000;
		Settings->Tolerance = 1e-5;
		return Settings;
	}

	struct FResult
	{
		const UPCGBasePointData* Flow = nullptr;
		const UPCGBasePointData* Cells = nullptr;
		FPCGVoxelGrid FlowGrid;
		bool bFlowReads = false;
		TArray<FString> FlowTags;
	};

	FResult RunFlow(UPCGVoxelAirFlowSettings* Settings, const UPCGData* Set, const UPCGData* SourceSet = nullptr)
	{
		FResult Result;
		TArray<TPair<FName, const UPCGData*>> Inputs = { { PCGPinConstants::DefaultInputLabel, Set } };
		if (SourceSet)
		{
			Inputs.Emplace(PCGVoxelAirFlowPins::Sources, SourceSet);
		}
		for (const FPCGTaggedData& Output : Run<FPCGVoxelAirFlowElement>(Settings, Inputs))
		{
			if (Output.Pin == PCGVoxelAirFlowPins::Flow)
			{
				Result.Flow = Cast<UPCGBasePointData>(Output.Data);
				Result.FlowTags = Output.Tags.Array();
			}
			else if (Output.Pin == PCGVoxelAirFlowPins::Cells)
			{
				Result.Cells = Cast<UPCGBasePointData>(Output.Data);
			}
		}
		if (Result.Flow)
		{
			FPCGContext Context;
			Result.bFlowReads = FPCGVoxelGrid::Build(&Context, Result.Flow, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Result.FlowGrid);
		}
		return Result;
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Default)
	{
		const FPCGMetadataDomain* Domain = Points && Index >= 0 ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Default;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}

	/** Flow at an air cell (air cell indices), or a NaN vector when that cell is not in the output. */
	FVector FlowAt(const FResult& Result, const FIntVector& Cell)
	{
		const int32 Point = Result.bFlowReads ? Result.FlowGrid.FindPoint(Cell) : INDEX_NONE;
		return Point == INDEX_NONE ? FVector(NAN) : ValueOf<FVector>(Result.Flow, TEXT("Flow"), Point, FVector(NAN));
	}

	double ShelterAt(const FResult& Result, const FIntVector& Cell)
	{
		const int32 Point = Result.bFlowReads ? Result.FlowGrid.FindPoint(Cell) : INDEX_NONE;
		return ValueOf<double>(Result.Flow, TEXT("Shelter"), Point, -1.0);
	}

	/** Every cell of a hollow N^3 box (one cell thick). */
	TArray<FIntVector> Shell(int32 N)
	{
		TArray<FIntVector> Cells;
		for (const FIntVector& Cell : Block(N, N, N))
		{
			if (Cell.X == 0 || Cell.Y == 0 || Cell.Z == 0 || Cell.X == N - 1 || Cell.Y == N - 1 || Cell.Z == N - 1)
			{
				Cells.Add(Cell);
			}
		}
		return Cells;
	}

	/** One flow source point: a box of half-size Radius at Location, turned by Rotation, with the given attributes (NAN = absent). */
	struct FSourceSpec
	{
		FVector Location = FVector::ZeroVector;
		double Radius = 250.0;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Force = FVector(NAN);
		double Outflow = NAN;
		double Swirl = NAN;
	};

	UPCGPointArrayData* MakeSources(const TArray<FSourceSpec>& Specs)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Specs.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGMetadataDomain* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		auto Transforms = Points->GetTransformValueRange();
		auto Mins = Points->GetBoundsMinValueRange();
		auto Maxs = Points->GetBoundsMaxValueRange();
		auto Keys = Points->GetMetadataEntryValueRange();
		for (int32 I = 0; I < Specs.Num(); ++I)
		{
			const FSourceSpec& Spec = Specs[I];
			Transforms[I] = FTransform(Spec.Rotation, Spec.Location);
			Mins[I] = -FVector(Spec.Radius);
			Maxs[I] = FVector(Spec.Radius);
			Keys[I] = Elements->AddEntry();
			if (!Spec.Force.ContainsNaN())
			{
				FPCGMetadataAttribute<FVector>* A = Elements->FindOrCreateAttribute<FVector>(TEXT("FlowForce"), FVector::ZeroVector, false);
				A->SetValue(Keys[I], Spec.Force);
			}
			if (!FMath::IsNaN(Spec.Outflow))
			{
				Elements->FindOrCreateAttribute<double>(TEXT("FlowOutflow"), 0.0, false)->SetValue(Keys[I], Spec.Outflow);
			}
			if (!FMath::IsNaN(Spec.Swirl))
			{
				Elements->FindOrCreateAttribute<double>(TEXT("FlowSwirl"), 0.0, false)->SetValue(Keys[I], Spec.Swirl);
			}
		}
		return Points;
	}

	/** One cell at the origin with 8 air cells of padding: room for sources in free air at x = -400. */
	UPCGVoxelAirFlowSettings* StillAirSettings()
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->Wind.Constant = FVector::ZeroVector;
		Settings->Padding = 8;
		return Settings;
	}

	int32 IndexOfCell(const TArray<FIntVector>& Cells, const FIntVector& Cell)
	{
		return Cells.IndexOfByKey(Cell);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowAroundTest,
	"PCGUtils.Voxel.AirFlow.GoesAroundSolids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowAroundTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	// A wall one cell thick across the wind: x = 0, y and z from 0 to 4.
	TArray<FIntVector> Wall;
	for (int32 Z = 0; Z < 5; ++Z)
		for (int32 Y = 0; Y < 5; ++Y)
			Wall.Add(FIntVector(0, Y, Z));
	UPCGVoxelAirFlowSettings* Settings = MakeSettings();
	Settings->Padding = 6;
	const FResult Result = RunFlow(Settings, MakeVoxels(Wall));

	UTEST_NOT_NULL(TEXT("Flow came out"), Result.Flow);
	UTEST_TRUE(TEXT("Flow is a voxel set of its own"), Result.bFlowReads);
	TestEqual(TEXT("One point per air cell, solids left out"), Result.Flow->GetNumPoints(), 13 * 17 * 17 - 25);
	TestTrue(TEXT("Flow keeps the input's tags"), Result.FlowTags.Contains(TEXT("KeptTag")));
	TestTrue(TEXT("Air cells are voxel sized"), Result.FlowGrid.VoxelSize.Equals(FVector(100.0)));

	const FVector Front = FlowAt(Result, FIntVector(-1, 2, 2));
	AddInfo(FString::Printf(TEXT("front %s"), *Front.ToString()));
	TestTrue(TEXT("In front of the wall the wind slows down (stagnation)"), Front.X > 0.0 && Front.X < 50.0);
	TestTrue(TEXT("In front of the wall's middle the flow does not lean sideways (symmetry)"), FMath::Abs(Front.Y) < 2.0 && FMath::Abs(Front.Z) < 2.0);

	const FVector Side = FlowAt(Result, FIntVector(0, -1, 2));
	const FVector Top = FlowAt(Result, FIntVector(0, 2, 5));
	AddInfo(FString::Printf(TEXT("side %s top %s"), *Side.ToString(), *Top.ToString()));
	TestTrue(TEXT("Beside the wall's edge the wind speeds up"), Side.Size() > 110.0);
	TestTrue(TEXT("Over the wall's top the wind speeds up"), Top.Size() > 110.0);
	TestTrue(TEXT("Beside the wall the flow bends outward (-Y)"), FlowAt(Result, FIntVector(-1, -1, 2)).Y < -5.0);

	const FVector Far = FlowAt(Result, FIntVector(-6, -6, -6));
	TestTrue(TEXT("Far from the wall the wind is about the free wind"), FMath::Abs(Far.X - 100.0) < 25.0);

	// Air never comes out of nowhere: as much air leaves the box as enters it.
	double Balance = 0.0;
	const TConstPCGValueRange<FTransform> Transforms = Result.Flow->GetConstTransformValueRange();
	for (int32 Point = 0; Point < Result.Flow->GetNumPoints(); ++Point)
	{
		const FVector P = Transforms[Point].GetLocation() / 100.0;
		const FVector V = ValueOf<FVector>(Result.Flow, TEXT("Flow"), Point, FVector::ZeroVector);
		Balance += (FMath::IsNearlyEqual(P.X, 6.0) ? V.X : 0.0) - (FMath::IsNearlyEqual(P.X, -6.0) ? V.X : 0.0)
			+ (FMath::IsNearlyEqual(P.Y, 10.0) ? V.Y : 0.0) - (FMath::IsNearlyEqual(P.Y, -6.0) ? V.Y : 0.0)
			+ (FMath::IsNearlyEqual(P.Z, 10.0) ? V.Z : 0.0) - (FMath::IsNearlyEqual(P.Z, -6.0) ? V.Z : 0.0);
	}
	AddInfo(FString::Printf(TEXT("net outflow of edge cells %.2f"), Balance));

	UTEST_NOT_NULL(TEXT("Cells came out"), Result.Cells);
	TestEqual(TEXT("Cells is the input point for point"), Result.Cells->GetNumPoints(), Wall.Num());
	const int32 Middle = IndexOfCell(Wall, FIntVector(0, 2, 2));
	TestEqual(TEXT("A wall square on to the open wind has exposure 1"), ValueOf<double>(Result.Cells, TEXT("WindExposure"), Middle, -1.0), 1.0);
	TestEqual(TEXT("Cells keeps the input's attributes"), ValueOf<int32>(Result.Cells, TEXT("Id"), Middle, -1), Middle);
	const FVector Surface = ValueOf<FVector>(Result.Cells, TEXT("SurfaceFlow"), IndexOfCell(Wall, FIntVector(0, 0, 2)), FVector::ZeroVector);
	AddInfo(FString::Printf(TEXT("edge surface flow %s"), *Surface.ToString()));
	TestTrue(TEXT("Round the wall's edge the surface flow runs downwind, fast"), Surface.X > 50.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowShadowTest,
	"PCGUtils.Voxel.AirFlow.ShadowsSealedAirAndFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowShadowTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	const TArray<FIntVector> Cube = Block(3, 3, 3);
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->WakeLength = 6.0;
		Settings->WakeStrength = 0.8;
		const FResult Result = RunFlow(Settings, MakeVoxels(Cube));
		UTEST_TRUE(TEXT("Flow reads"), Result.bFlowReads);
		TestEqual(TEXT("Right behind the cube: full shelter"), ShelterAt(Result, FIntVector(3, 1, 1)), 1.0, 1e-6);
		TestEqual(TEXT("Three cells behind: shelter fades (1 - 2/6)"), ShelterAt(Result, FIntVector(5, 1, 1)), 1.0 - 2.0 / 6.0, 1e-5);
		TestEqual(TEXT("In front of the cube: no shelter"), ShelterAt(Result, FIntVector(-1, 1, 1)), 0.0, 1e-6);
		const double Behind = FlowAt(Result, FIntVector(4, 1, 1)).Size();
		AddInfo(FString::Printf(TEXT("speed behind %.2f"), Behind));
		TestTrue(TEXT("Behind the cube the wind is weak"), Behind < 60.0);

		UTEST_NOT_NULL(TEXT("Cells came out"), Result.Cells);
		TestEqual(TEXT("The upwind face is fully exposed"), ValueOf<double>(Result.Cells, TEXT("WindExposure"), IndexOfCell(Cube, FIntVector(0, 1, 1)), -1.0), 1.0);
		TestEqual(TEXT("The downwind face is not exposed"), ValueOf<double>(Result.Cells, TEXT("WindExposure"), IndexOfCell(Cube, FIntVector(2, 1, 1)), -1.0), 0.0);
	}
	{
		// A hollow box: the air inside cannot be reached, so it is still and fully sheltered.
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		const FResult Result = RunFlow(Settings, MakeVoxels(Shell(5)));
		UTEST_TRUE(TEXT("Flow reads"), Result.bFlowReads);
		TestTrue(TEXT("Sealed air does not move"), FlowAt(Result, FIntVector(2, 2, 2)).IsNearlyZero());
		TestEqual(TEXT("Sealed air is fully sheltered"), ShelterAt(Result, FIntVector(2, 2, 2)), 1.0);
		TestTrue(TEXT("Air outside the box moves"), FlowAt(Result, FIntVector(-2, 2, 2)).X > 10.0);
	}
	{
		// Standing on the ground: no air below the lowest cells, and air at the floor runs along it.
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->bGroundAtBottom = true;
		const FResult Result = RunFlow(Settings, MakeVoxels(Cube));
		UTEST_TRUE(TEXT("Flow reads"), Result.bFlowReads);
		TestEqual(TEXT("No air below the floor"), Result.FlowGrid.MinCell.Z, 0);
		TestEqual(TEXT("Air cell count with the floor"), Result.Flow->GetNumPoints(), 11 * 11 * 7 - 27);
		const FVector Floor = FlowAt(Result, FIntVector(-4, 1, 0));
		AddInfo(FString::Printf(TEXT("floor %s"), *Floor.ToString()));
		TestTrue(TEXT("Upwind at the floor the wind still blows"), Floor.X > 50.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowCoarseTest,
	"PCGUtils.Voxel.AirFlow.CoarseCellsDistanceAndTurbulence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowCoarseTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	const UPCGPointArrayData* Cube = MakeVoxels(Block(4, 4, 4));
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->AirCellVoxels = 2;
		const FResult Result = RunFlow(Settings, Cube);
		UTEST_TRUE(TEXT("Coarse flow reads as a voxel set"), Result.bFlowReads);
		TestTrue(TEXT("Air cells are two voxels wide"), Result.FlowGrid.VoxelSize.Equals(FVector(200.0)));
		TestEqual(TEXT("Coarse air cell count"), Result.Flow->GetNumPoints(), 10 * 10 * 10 - 8);
		const int32 Point = Result.FlowGrid.FindPoint(FIntVector(-1, 0, 0));
		UTEST_TRUE(TEXT("Air cell (-1, 0, 0) is there"), Point != INDEX_NONE);
		TestTrue(TEXT("It sits over voxels -2 and -1 (x = -150)"),
			Result.Flow->GetConstTransformValueRange()[Point].GetLocation().Equals(FVector(-150.0, 50.0, 50.0), 1e-3));
	}
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->MaxDistance = 1;
		Settings->bWriteSolidDistance = true;
		const FResult Result = RunFlow(Settings, Cube);
		UTEST_NOT_NULL(TEXT("Flow came out"), Result.Flow);
		TestEqual(TEXT("Max Distance 1 keeps only air touching a face"), Result.Flow->GetNumPoints(), 6 * 16);
		bool bAllOne = true;
		for (int32 P = 0; P < Result.Flow->GetNumPoints(); ++P)
		{
			bAllOne &= ValueOf<int32>(Result.Flow, TEXT("SolidDistance"), P, -1) == 1;
		}
		TestTrue(TEXT("Every kept cell is at distance 1"), bAllOne);
	}
	{
		UPCGVoxelAirFlowSettings* Calm = MakeSettings();
		UPCGVoxelAirFlowSettings* Gusty = MakeSettings();
		Gusty->TurbulenceStrength = 0.5;
		Gusty->TurbulenceSize = 300.0;
		const FResult A = RunFlow(Calm, Cube);
		const FResult B = RunFlow(Gusty, Cube);
		const FResult C = RunFlow(Gusty, Cube);
		UTEST_TRUE(TEXT("Both read"), A.bFlowReads && B.bFlowReads && C.bFlowReads);
		double Difference = 0.0;
		bool bFinite = true, bRepeatable = true;
		for (int32 P = 0; P < B.Flow->GetNumPoints(); ++P)
		{
			const FVector VA = ValueOf<FVector>(A.Flow, TEXT("Flow"), P, FVector::ZeroVector);
			const FVector VB = ValueOf<FVector>(B.Flow, TEXT("Flow"), P, FVector::ZeroVector);
			bFinite &= !VB.ContainsNaN();
			bRepeatable &= VB.Equals(ValueOf<FVector>(C.Flow, TEXT("Flow"), P, FVector::ZeroVector), 1e-4);
			Difference = FMath::Max(Difference, (VA - VB).Size());
		}
		TestTrue(TEXT("Turbulence changes the flow"), Difference > 5.0);
		TestTrue(TEXT("Turbulent flow has no NaN"), bFinite);
		TestTrue(TEXT("Same seed, same turbulence"), bRepeatable);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowErrorsTest,
	"PCGUtils.Voxel.AirFlow.Errors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowErrorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	const UPCGPointArrayData* Cube = MakeVoxels(Block(2, 2, 2));
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->Wind.Constant = FVector::ZeroVector;
		AddExpectedError(TEXT("it must not be zero"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("Zero wind gives no flow"), RunFlow(Settings, Cube).Flow);
	}
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->MaxAirCells = 100;
		AddExpectedError(TEXT("the air box is 10 x 10 x 10 = 1,000 air cells, more than Max Air Cells (100)"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("Too much air gives no flow"), RunFlow(Settings, Cube).Flow);
	}
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->AirCellVoxels = 17;
		AddExpectedError(TEXT("Air Cell Voxels is 17; it must be from 1 to 16."), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("A bad Air Cell Voxels gives no flow"), RunFlow(Settings, Cube).Flow);
	}
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->SpeedAttribute = TEXT("Flow");
		AddExpectedError(TEXT("Flow Attribute and Speed Attribute are both 'Flow'"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("Clashing names give no flow"), RunFlow(Settings, Cube).Flow);
	}
	{
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->Wind.Input = EPCGUtilsInputValueType::Attribute;
		AddExpectedError(TEXT("Wind"), EAutomationExpectedErrorFlags::Contains, 0, false);
		TestNull(TEXT("A missing @Data.Wind gives no flow"), RunFlow(Settings, Cube).Flow);
	}
	{
		// Not an error: too few solver steps still gives a flow, with a warning naming the limit.
		UPCGVoxelAirFlowSettings* Settings = MakeSettings();
		Settings->MaxIterations = 1;
		AddExpectedMessagePlain(TEXT("the solve stopped at Max Iterations (1)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestNotNull(TEXT("An unfinished solve still gives a flow"), RunFlow(Settings, Cube).Flow);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowSourcesTest,
	"PCGUtils.Voxel.AirFlow.SourcesPushVentAndSwirl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowSourcesTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	const UPCGPointArrayData* Cell = MakeVoxels({ FIntVector(0, 0, 0) });
	{
		// A vent at x = -400 with no wind: air leaves it in every direction.
		FSourceSpec Vent;
		Vent.Location = FVector(-400.0, 0.0, 0.0);
		Vent.Outflow = 100.0;
		const FResult Result = RunFlow(StillAirSettings(), Cell, MakeSources({ Vent }));
		UTEST_TRUE(TEXT("Sources alone give a flow (zero wind is allowed with sources)"), Result.bFlowReads);
		const FVector East = FlowAt(Result, FIntVector(-2, 0, 0)), West = FlowAt(Result, FIntVector(-6, 0, 0)), Up = FlowAt(Result, FIntVector(-4, 0, 2));
		AddInfo(FString::Printf(TEXT("vent east %s west %s up %s"), *East.ToString(), *West.ToString(), *Up.ToString()));
		TestTrue(TEXT("East of the vent the air flows east"), East.X > 10.0);
		TestTrue(TEXT("West of the vent the air flows west"), West.X < -10.0);
		TestTrue(TEXT("Above the vent the air rises"), Up.Z > 10.0);
		TestTrue(TEXT("Air slows with distance from the vent"), FlowAt(Result, FIntVector(-7, 0, 0)).Size() < West.Size());

		// The same source with negative outflow sucks air in.
		Vent.Outflow = -100.0;
		const FResult Sink = RunFlow(StillAirSettings(), Cell, MakeSources({ Vent }));
		TestTrue(TEXT("East of a sink the air flows west, into it"), FlowAt(Sink, FIntVector(-2, 0, 0)).X < -10.0);
	}
	{
		// A vortex about +Z: counter-clockwise seen from above.
		FSourceSpec Vortex;
		Vortex.Location = FVector(-400.0, 0.0, 0.0);
		Vortex.Radius = 300.0;
		Vortex.Swirl = 100.0;
		const FResult Result = RunFlow(StillAirSettings(), Cell, MakeSources({ Vortex }));
		UTEST_TRUE(TEXT("Swirl flow reads"), Result.bFlowReads);
		const FVector East = FlowAt(Result, FIntVector(-2, 0, 0)), North = FlowAt(Result, FIntVector(-4, 2, 0));
		AddInfo(FString::Printf(TEXT("swirl east %s north %s"), *East.ToString(), *North.ToString()));
		TestTrue(TEXT("East of the vortex the air goes north (+Y)"), East.Y > 10.0);
		TestTrue(TEXT("North of the vortex the air goes west (-X)"), North.X < -10.0);
		TestTrue(TEXT("A vortex about Z does not lift the air"), FMath::Abs(East.Z) < 1.0);
	}
	{
		// A fan pushing along world +Y, and a fan pushing along its own X turned a quarter about Z: both blow +Y.
		FSourceSpec Fan;
		Fan.Location = FVector(-400.0, 0.0, 0.0);
		Fan.Force = FVector(0.0, 100.0, 0.0);
		const FResult World = RunFlow(StillAirSettings(), Cell, MakeSources({ Fan }));
		const FVector AtWorldFan = FlowAt(World, FIntVector(-4, 0, 0));
		Fan.Force = FVector(100.0, 0.0, 0.0);
		Fan.Rotation = FRotator(0.0, 90.0, 0.0);
		UPCGVoxelAirFlowSettings* PointSpace = StillAirSettings();
		PointSpace->ForceSpace = EPCGVoxelFlowSourceSpace::Point;
		const FVector AtPointFan = FlowAt(RunFlow(PointSpace, Cell, MakeSources({ Fan })), FIntVector(-4, 0, 0));
		AddInfo(FString::Printf(TEXT("fan world %s point %s"), *AtWorldFan.ToString(), *AtPointFan.ToString()));
		TestTrue(TEXT("A world fan blows along its Force"), AtWorldFan.Y > 20.0);
		TestTrue(TEXT("A Point-space fan blows along its turned forward"), AtPointFan.Y > 20.0 && FMath::Abs(AtPointFan.X) < 0.2 * AtPointFan.Y);
		TestTrue(TEXT("World and turned Point-space fans agree"), AtWorldFan.Equals(AtPointFan, 1.0));
	}
	{
		// Sources add to the wind.
		FSourceSpec Vent;
		Vent.Location = FVector(-400.0, 0.0, 0.0);
		Vent.Outflow = 100.0;
		UPCGVoxelAirFlowSettings* Windy = StillAirSettings();
		Windy->Wind.Constant = FVector(100.0, 0.0, 0.0);
		const FResult Result = RunFlow(Windy, Cell, MakeSources({ Vent }));
		TestTrue(TEXT("Downwind of a vent the air is faster than the wind"), FlowAt(Result, FIntVector(-2, 0, 0)).X > 105.0);
		TestTrue(TEXT("Upwind of a vent the air is slower than the wind"), FlowAt(Result, FIntVector(-6, 0, 0)).X < 95.0);
	}
	{
		// With no wind, exposure is measured against the strongest source: a fan aimed at the cell hits its west side.
		FSourceSpec Fan;
		Fan.Location = FVector(-300.0, 0.0, 0.0);
		Fan.Radius = 400.0;
		Fan.Force = FVector(100.0, 0.0, 0.0);
		const FResult Result = RunFlow(StillAirSettings(), Cell, MakeSources({ Fan }));
		UTEST_NOT_NULL(TEXT("Cells came out"), Result.Cells);
		const double Exposure = ValueOf<double>(Result.Cells, TEXT("WindExposure"), 0, -1.0);
		AddInfo(FString::Printf(TEXT("fan exposure %.3f"), Exposure));
		TestTrue(TEXT("A fan aimed at a cell exposes it"), Exposure > 0.3 && Exposure <= 1.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowSourceErrorsTest,
	"PCGUtils.Voxel.AirFlow.SourceErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowSourceErrorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelAirFlowTests;
	const UPCGPointArrayData* Cell = MakeVoxels({ FIntVector(0, 0, 0) });
	{
		FSourceSpec Bare;
		AddExpectedError(TEXT("Sources input 0: none of Force Attribute 'FlowForce', Outflow Attribute 'FlowOutflow' and Swirl Attribute 'FlowSwirl' exists on its points"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("A source with no effect attribute gives no flow"), RunFlow(StillAirSettings(), Cell, MakeSources({ Bare })).Flow);
	}
	{
		FSourceSpec Flat;
		Flat.Radius = 0.0;
		Flat.Outflow = 10.0;
		AddExpectedError(TEXT("Sources input 0: point 0: its largest scaled half-size is 0"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("A source with no bounds gives no flow"), RunFlow(StillAirSettings(), Cell, MakeSources({ Flat })).Flow);
	}
	{
		FSourceSpec Vent;
		Vent.Outflow = 10.0;
		UPCGVoxelAirFlowSettings* Settings = StillAirSettings();
		Settings->bRadiusFromBounds = false;
		AddExpectedError(TEXT("Radius Attribute 'FlowRadius' does not exist on the points."), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestNull(TEXT("A missing radius attribute gives no flow"), RunFlow(Settings, Cell, MakeSources({ Vent })).Flow);
	}
	return true;
}

#endif
