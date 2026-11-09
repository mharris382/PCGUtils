// Copyright Max Harris
//
// Voxel | Transform Grid: sets stay valid voxel sets, points follow the grid, per-set @Data values and pivots.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelToDynMesh.h"
#include "Elements/PCGVoxelTransformGrid.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelTransformGridTests
{
	using namespace PCGVoxelTestHelpers;

	UPCGVoxelTransformGridSettings* MakeSettings()
	{
		return NewObject<UPCGVoxelTransformGridSettings>();
	}

	const UPCGBasePointData* PointsOf(const TArray<FPCGTaggedData>& Outputs, int32 Index = 0)
	{
		return Outputs.IsValidIndex(Index) ? Cast<UPCGBasePointData>(Outputs[Index].Data) : nullptr;
	}

	/** Reads Points as a voxel set with default settings (off-grid points are errors). */
	bool Reads(const UPCGBasePointData* Points, FPCGVoxelGrid& OutGrid)
	{
		FPCGContext Context;
		return Points && FPCGVoxelGrid::Build(&Context, Points, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, OutGrid);
	}

	double MeshVolume(const UPCGData* Points)
	{
		UPCGVoxelToDynMeshSettings* Mesher = NewObject<UPCGVoxelToDynMeshSettings>();
		Mesher->bConvertToLocalSpace = false;
		const UE::Geometry::FDynamicMesh3* Mesh = MeshOf(Run<FPCGVoxelToDynMeshElement>(Mesher, { { PCGPinConstants::DefaultInputLabel, Points } }));
		return Mesh ? Volume(*Mesh) : -1.0;
	}

	template <typename T>
	void WriteData(UPCGBasePointData* Points, FName Name, const T& Value)
	{
		if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Points))
		{
			PCGUtilsDataDomain::WriteValue(*Domain, Name, Value);
		}
	}

	TArray<FPCGTaggedData> Transform(UPCGVoxelTransformGridSettings* Settings, const TArray<const UPCGData*>& Sets)
	{
		TArray<TPair<FName, const UPCGData*>> Inputs;
		for (const UPCGData* Set : Sets)
		{
			Inputs.Emplace(PCGPinConstants::DefaultInputLabel, Set);
		}
		return Run<FPCGVoxelTransformGridElement>(Settings, Inputs);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelTransformGridTurnTest,
	"PCGUtils.Voxel.TransformGrid.TurnedSetsStayVoxelSets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelTransformGridTurnTest::RunTest(const FString&)
{
	using namespace PCGVoxelTransformGridTests;
	// A row of three cells along X, centres at x = 0, 100, 200. Its bounds centre is (100, 0, 0).
	const UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0) });

	// A quarter turn about Z through the bounds centre stands the row along Y.
	UPCGVoxelTransformGridSettings* Settings = MakeSettings();
	Settings->Rotation.Constant = FRotator(0.0, 90.0, 0.0);
	const TArray<FPCGTaggedData> Outputs = Transform(Settings, { Row });
	const UPCGBasePointData* Turned = PointsOf(Outputs);
	UTEST_NOT_NULL(TEXT("A set came out"), Turned);
	const TConstPCGValueRange<FTransform> Transforms = Turned->GetConstTransformValueRange();
	for (int32 I = 0; I < 3; ++I)
	{
		const FVector Expected(100.0, -100.0 + 100.0 * I, 0.0);
		TestTrue(*FString::Printf(TEXT("Cell %d moves to %s (got %s)"), I, *Expected.ToString(), *Transforms[I].GetLocation().ToString()),
			Transforms[I].GetLocation().Equals(Expected, 1e-6));
	}
	TestTrue(TEXT("The output keeps the input's tags"), Outputs[0].Tags.Contains(TEXT("KeptTag")));
	const FPCGMetadataAttribute<int32>* Id = Turned->Metadata->GetConstTypedAttribute<int32>(TEXT("Id"));
	TestTrue(TEXT("The output keeps per-point attributes"), Id && Id->GetValueFromItemKey(Turned->GetConstMetadataEntryValueRange()[2]) == 2);

	// An odd angle: the set still reads with no off-grid points, keeps its cells, and meshes to the same volume.
	Settings = MakeSettings();
	Settings->Rotation.Constant = FRotator(17.0, 31.0, -43.0);
	Settings->Translation.Constant = FVector(12.5, -300.0, 47.0);
	const UPCGPointArrayData* Lumpy = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(1, 1, 0), FIntVector(1, 1, 1), FIntVector(5, 2, -3) });
	const UPCGBasePointData* Odd = PointsOf(Transform(Settings, { Lumpy }));
	FPCGVoxelGrid Before, After;
	UTEST_TRUE(TEXT("The input reads"), Reads(Lumpy, Before));
	UTEST_TRUE(TEXT("The turned set reads as a voxel set (no off-grid points)"), Reads(Odd, After));
	TestEqual(TEXT("Same cells"), After.Cells, Before.Cells);
	TestTrue(TEXT("Same volume"), FMath::IsNearlyEqual(MeshVolume(Odd), 5.0e6, 1.0));

	// Point rotations follow the grid, so the cell boxes line up with the new grid axes.
	const FQuat GridRotation = After.GridToWorld.GetRotation();
	TestTrue(TEXT("Points turn with the grid"), Odd->GetConstTransformValueRange()[0].GetRotation().Equals(GridRotation, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelTransformGridDataTest,
	"PCGUtils.Voxel.TransformGrid.EachSetUsesItsOwnDataAndPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelTransformGridDataTest::RunTest(const FString&)
{
	using namespace PCGVoxelTransformGridTests;
	// Two slices of a corridor, each with its own rotation and pivot in @Data.
	UPCGPointArrayData* A = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(0, 1, 0) });
	UPCGPointArrayData* B = MakeVoxels({ FIntVector(1, 0, 0), FIntVector(1, 1, 0) });
	WriteData(A, TEXT("Rotation"), FRotator(0.0, 0.0, 90.0));
	WriteData(B, TEXT("Rotation"), FRotator(0.0, 0.0, 180.0));
	WriteData(A, TEXT("PivotAlignment"), FVector(0.5, 0.0, 0.5)); // the slice's -Y side: (0, -50, 0)
	WriteData(B, TEXT("PivotAlignment"), FVector(0.5, 0.5, 0.5)); // its centre: (100, 50, 0)

	UPCGVoxelTransformGridSettings* Settings = MakeSettings();
	Settings->Rotation.Input = EPCGUtilsInputValueType::Attribute;
	Settings->PivotAlignment.Input = EPCGUtilsInputValueType::Attribute;
	const TArray<FPCGTaggedData> Outputs = Transform(Settings, { A, B });
	UTEST_EQUAL(TEXT("One set out per set in"), Outputs.Num(), 2);

	// A: a quarter roll about X through (0, -50, 0), so both cells end up straight above or below the pivot.
	const TConstPCGValueRange<FTransform> TA = PointsOf(Outputs, 0)->GetConstTransformValueRange();
	const FVector PivotA(0.0, -50.0, 0.0);
	const FQuat RollA = FRotator(0.0, 0.0, 90.0).Quaternion();
	const FVector A0 = PivotA + RollA.RotateVector(FVector(0.0, 0.0, 0.0) - PivotA);
	const FVector A1 = PivotA + RollA.RotateVector(FVector(0.0, 100.0, 0.0) - PivotA);
	TestTrue(TEXT("A's cells end up in line above or below its pivot"), FMath::IsNearlyEqual(FMath::Abs(A0.Z), 50.0, 1e-6) && FMath::IsNearlyEqual(A0.Y, -50.0, 1e-6));
	TestTrue(*FString::Printf(TEXT("A cell 0 at %s (got %s)"), *A0.ToString(), *TA[0].GetLocation().ToString()), TA[0].GetLocation().Equals(A0, 1e-6));
	TestTrue(*FString::Printf(TEXT("A cell 1 at %s (got %s)"), *A1.ToString(), *TA[1].GetLocation().ToString()), TA[1].GetLocation().Equals(A1, 1e-6));

	// B: a half roll about its centre swaps its two cells.
	const TConstPCGValueRange<FTransform> TB = PointsOf(Outputs, 1)->GetConstTransformValueRange();
	TestTrue(*FString::Printf(TEXT("B cell 0 (got %s)"), *TB[0].GetLocation().ToString()), TB[0].GetLocation().Equals(FVector(100.0, 100.0, 0.0), 1e-6));
	TestTrue(*FString::Printf(TEXT("B cell 1 (got %s)"), *TB[1].GetLocation().ToString()), TB[1].GetLocation().Equals(FVector(100.0, 0.0, 0.0), 1e-6));

	// A Location pivot from @Data, in world space.
	WriteData(A, TEXT("Pivot"), FVector(0.0, 0.0, 0.0));
	Settings = MakeSettings();
	Settings->Rotation.Constant = FRotator(0.0, 180.0, 0.0);
	Settings->Pivot = EPCGVoxelTransformPivot::Location;
	Settings->PivotLocation.Input = EPCGUtilsInputValueType::Attribute;
	Settings->Space = EPCGVoxelTransformSpace::World;
	const TConstPCGValueRange<FTransform> TL = PointsOf(Transform(Settings, { A }))->GetConstTransformValueRange();
	TestTrue(*FString::Printf(TEXT("Location pivot: cell 1 turns about the origin (got %s)"), *TL[1].GetLocation().ToString()),
		TL[1].GetLocation().Equals(FVector(0.0, -100.0, 0.0), 1e-6));

	// Grid space follows the set's grid: on a grid turned a quarter about Z, a grid-space roll (about grid X) is a
	// rotation about the grid's X as seen in the world; both give the same points.
	UPCGPointArrayData* Turned = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(1, 0, 1) });
	const FTransform GridToWorld(FRotator(0.0, 90.0, 0.0));
	{
		// Put the points where that grid has them.
		TPCGValueRange<FTransform> Transforms = Turned->GetTransformValueRange();
		for (int32 I = 0; I < Transforms.Num(); ++I)
		{
			Transforms[I] = FTransform(GridToWorld.GetRotation(), GridToWorld.TransformPositionNoScale(Transforms[I].GetLocation()));
		}
		WriteData(Turned, TEXT("VoxelGridTransform"), GridToWorld);
	}
	UPCGVoxelTransformGridSettings* InGrid = MakeSettings();
	InGrid->Rotation.Constant = FRotator(0.0, 0.0, 37.0);
	InGrid->Pivot = EPCGVoxelTransformPivot::Location;
	UPCGVoxelTransformGridSettings* InWorld = MakeSettings();
	InWorld->Space = EPCGVoxelTransformSpace::World;
	InWorld->Pivot = EPCGVoxelTransformPivot::Location;
	InWorld->Rotation.Constant = (GridToWorld.GetRotation() * FRotator(0.0, 0.0, 37.0).Quaternion() * GridToWorld.GetRotation().Inverse()).Rotator();
	const TConstPCGValueRange<FTransform> G1 = PointsOf(Transform(InGrid, { Turned }))->GetConstTransformValueRange();
	const TConstPCGValueRange<FTransform> G2 = PointsOf(Transform(InWorld, { Turned }))->GetConstTransformValueRange();
	for (int32 I = 0; I < 3; ++I)
	{
		TestTrue(*FString::Printf(TEXT("Grid space and the matching world rotation agree on cell %d"), I), G1[I].GetLocation().Equals(G2[I].GetLocation(), 1e-4));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelTransformGridScaleTest,
	"PCGUtils.Voxel.TransformGrid.ScaleAndErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelTransformGridScaleTest::RunTest(const FString&)
{
	using namespace PCGVoxelTransformGridTests;
	const UPCGPointArrayData* Block = MakeVoxels(PCGVoxelTestHelpers::Block(2, 2, 1));

	// Scale (2, 1, 3) about the min corner: Voxel Size becomes (200, 100, 300), the set still reads, volume x6.
	UPCGVoxelTransformGridSettings* Settings = MakeSettings();
	Settings->Scale.Constant = FVector(2.0, 1.0, 3.0);
	Settings->PivotAlignment.Constant = FVector(0.0);
	const UPCGBasePointData* Scaled = PointsOf(Transform(Settings, { Block }));
	FPCGVoxelGrid Grid;
	UTEST_TRUE(TEXT("The scaled set reads"), Reads(Scaled, Grid));
	TestTrue(*FString::Printf(TEXT("Voxel Size is (200, 100, 300) (got %s)"), *Grid.VoxelSize.ToString()), Grid.VoxelSize.Equals(FVector(200.0, 100.0, 300.0)));
	TestTrue(TEXT("The min corner stays put"), Scaled->GetConstTransformValueRange()[0].GetLocation().Equals(FVector(50.0, 0.0, 100.0), 1e-6));
	TestTrue(TEXT("Volume is six times larger"), FMath::IsNearlyEqual(MeshVolume(Scaled), 4.0e6 * 6.0, 1.0));

	// Errors, one per invariant.
	Settings->Scale.Constant = FVector(1.0, 0.0, 1.0);
	AddExpectedError(TEXT("Scale Y is 0; every axis must be greater than 0."), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("A zero scale makes no output"), Transform(Settings, { Block }).Num(), 0);

	Settings->Scale.Constant = FVector(2.0);
	Settings->Grid.VoxelSize.Input = EPCGUtilsInputValueType::Constant;
	AddExpectedError(TEXT("but Voxel Size is a Constant; read Voxel Size from @Data so the new size can be written there."), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("Scaling a constant-size set makes no output"), Transform(Settings, { Block }).Num(), 0);

	Settings = MakeSettings();
	Settings->Rotation.Input = EPCGUtilsInputValueType::Attribute;
	AddExpectedError(TEXT("Rotation"), EAutomationExpectedErrorFlags::Contains, 0, false);
	TestEqual(TEXT("A missing @Data.Rotation makes no output"), Transform(Settings, { Block }).Num(), 0);
	return true;
}

#endif
