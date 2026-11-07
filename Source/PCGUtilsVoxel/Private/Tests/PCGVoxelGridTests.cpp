// Copyright Max Harris
//
// The voxel contract: what FPCGVoxelGrid accepts, and the one exact error it gives for each thing it refuses.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelGridTests
{
	const FText Title = FText::FromString(TEXT("Voxel Test"));

	bool BuildGrid(const UPCGPointArrayData* Points, FPCGVoxelGrid& Grid, const FPCGVoxelGridReadSettings& Settings = FPCGVoxelGridReadSettings())
	{
		FPCGContext Context;
		return FPCGVoxelGrid::Build(&Context, Points, Settings, Title, 0, Grid);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelGridReadsCellsTest,
	"PCGUtils.Voxel.Grid.ReadsCellsAndChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelGridReadsCellsTest::RunTest(const FString&)
{
	using namespace PCGVoxelGridTests;
	using namespace PCGVoxelTestHelpers;

	FPCGVoxelGrid Grid;
	UTEST_TRUE(TEXT("Plain cells are read"), BuildGrid(MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(-1, 2, 3) }), Grid));
	TestEqual(TEXT("Three cells"), Grid.NumCells(), 3);
	TestFalse(TEXT("Not chunked"), Grid.bChunked);
	TestEqual(TEXT("A negative cell is found where it was placed"), Grid.FindPoint(FIntVector(-1, 2, 3)), 2);

	// A 2x2x2 chunk centred on a cell corner, beside a plain cell.
	const TArray<FIntVector> Spans = { FIntVector(2), FIntVector(1) };
	UTEST_TRUE(TEXT("A chunked set is read"), BuildGrid(MakeVoxels({ FIntVector(0, 0, 0), FIntVector(2, 0, 0) }, 100.0, &Spans), Grid));
	TestTrue(TEXT("Chunked"), Grid.bChunked);
	TestEqual(TEXT("The chunk expands to eight cells plus the plain one"), Grid.NumCells(), 9);
	TestEqual(TEXT("The chunk's far corner cell belongs to it"), Grid.FindPoint(FIntVector(1, 1, 1)), 0);
	TestEqual(TEXT("Its first cell is where it starts"), Grid.PointFirst[0], FIntVector(0, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelGridErrorsTest,
	"PCGUtils.Voxel.Grid.EachBrokenRuleHasItsOwnError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelGridErrorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelGridTests;
	using namespace PCGVoxelTestHelpers;
	FPCGVoxelGrid Grid;

	// Missing voxel size.
	{
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0) });
		PCGUtilsDataDomain::GetMutableDataDomain(Points)->DeleteAttribute(TEXT("VoxelSize"));
		AddExpectedError(TEXT("VoxelSize"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("No voxel size fails"), BuildGrid(Points, Grid));
	}
	// A zero axis.
	{
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0) });
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Points), TEXT("VoxelSize"), FVector(100, 0, 100));
		AddExpectedError(TEXT("Voxel Size Y is 0; every axis must be greater than 0."), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("A zero Y axis fails"), BuildGrid(Points, Grid));
	}
	// Off the grid.
	{
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0), FIntVector(3, 0, 0) });
		Points->GetTransformValueRange()[1].SetLocation(FVector(320, 0, 0));
		AddExpectedError(TEXT("point 1 is off the grid"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("An off-grid point fails"), BuildGrid(Points, Grid));

		FPCGVoxelGridReadSettings Snap;
		Snap.OffGridPoints = EPCGVoxelOffGridPoints::Snap;
		TestTrue(TEXT("Snap accepts it"), BuildGrid(Points, Grid, Snap));
		TestEqual(TEXT("and puts it in the nearest cell"), Grid.FindPoint(FIntVector(3, 0, 0)), 1);
	}
	// Two points in one cell.
	{
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0), FIntVector(1, 0, 0), FIntVector(0) });
		AddExpectedError(TEXT("points 0 and 2 both cover cell (0, 0, 0)"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("A duplicate cell fails"), BuildGrid(Points, Grid));

		FPCGVoxelGridReadSettings KeepLast;
		KeepLast.DuplicateCells = EPCGVoxelDuplicateCells::KeepLast;
		TestTrue(TEXT("Keep Last accepts it"), BuildGrid(Points, Grid, KeepLast));
		TestEqual(TEXT("and the later point has the cell"), Grid.FindPoint(FIntVector(0)), 2);
		TestFalse(TEXT("and the earlier point is dropped"), Grid.PointUsed[0]);
	}
	// Grid transform with scale.
	{
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0) });
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Points), TEXT("VoxelGridTransform"),
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0)));
		AddExpectedError(TEXT("it must be 1, because cell size comes from Voxel Size"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("A scaled grid transform fails"), BuildGrid(Points, Grid));
	}
	// A span that is not whole.
	{
		const TArray<FIntVector> Spans = { FIntVector(1) };
		UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0) }, 100.0, &Spans);
		FPCGMetadataAttribute<FVector>* Span = static_cast<FPCGMetadataAttribute<FVector>*>(
			Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)->GetMutableAttribute(TEXT("VoxelSpan")));
		Span->SetValue(Points->GetMetadataEntryValueRange()[0], FVector(1.5, 1, 1));
		AddExpectedError(TEXT("point 0: 'VoxelSpan' X is 1.5"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestFalse(TEXT("A fractional span fails"), BuildGrid(Points, Grid));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelGridTransformTest,
	"PCGUtils.Voxel.Grid.FollowsTheGridTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelGridTransformTest::RunTest(const FString&)
{
	using namespace PCGVoxelGridTests;
	using namespace PCGVoxelTestHelpers;

	// A grid turned 90 degrees about Z and moved: a point at world (1000, 100, 0) is grid cell (1, 0, 0).
	const FTransform GridToWorld(FRotator(0, 90, 0), FVector(1000, 0, 0));
	UPCGPointArrayData* Points = MakeVoxels({ FIntVector(0) });
	Points->GetTransformValueRange()[0].SetLocation(GridToWorld.TransformPosition(FVector(100, 0, 0)));
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Points), TEXT("VoxelGridTransform"), GridToWorld);
	FPCGVoxelGrid Grid;
	UTEST_TRUE(TEXT("Read"), BuildGrid(Points, Grid));
	TestEqual(TEXT("The point is grid cell (1, 0, 0)"), Grid.Cells[0], FIntVector(1, 0, 0));
	TestTrue(TEXT("and its world centre round-trips"), Grid.CellCenterWorld(FIntVector(1, 0, 0)).Equals(FVector(1000, 100, 0), 0.01));
	return true;
}

#endif
