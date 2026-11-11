// Copyright Max Harris
//
// Voxel | Shift Seams: seams move as wholes, the outside stays put, and the boxes still fill exactly the same space.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelCompress.h"
#include "Elements/PCGVoxelShiftSeams.h"
#include "Elements/PCGVoxelToDynMesh.h"
#include "Math/RandomStream.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelShiftSeamsTests
{
	using namespace PCGVoxelTestHelpers;

	UPCGVoxelShiftSeamsSettings* MakeSettings(float Strength, int32 Seed)
	{
		UPCGVoxelShiftSeamsSettings* Settings = NewObject<UPCGVoxelShiftSeamsSettings>();
		Settings->Strength.Constant = Strength;
		Settings->Seed = Seed;
		return Settings;
	}

	const UPCGBasePointData* Shift(UPCGVoxelShiftSeamsSettings* Settings, const UPCGData* Voxels)
	{
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelShiftSeamsElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
	}

	const UPCGBasePointData* CompressRandom(const UPCGData* Voxels, int32 Seed)
	{
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->Shape = EPCGVoxelChunkShape::Random;
		Settings->RandomSpan.Constant = FVector(3.0);
		Settings->Seed = Seed;
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
	}

	/** World box of every point (test points are unrotated with scale 1). */
	TArray<FBox> BoxesOf(const UPCGBasePointData* Points)
	{
		TArray<FBox> Boxes;
		const auto Transforms = Points->GetConstTransformValueRange();
		const auto Mins = Points->GetConstBoundsMinValueRange();
		const auto Maxs = Points->GetConstBoundsMaxValueRange();
		for (int32 I = 0; I < Points->GetNumPoints(); ++I)
		{
			const FVector P = Transforms[I].GetLocation();
			Boxes.Emplace(P + Mins[I], P + Maxs[I]);
		}
		return Boxes;
	}

	/** The box whose low corner is nearest Corner. */
	const FBox* BoxAt(const TArray<FBox>& Boxes, const FVector& Corner)
	{
		const FBox* Best = nullptr;
		for (const FBox& Box : Boxes)
		{
			if (!Best || FVector::DistSquared(Box.Min, Corner) < FVector::DistSquared(Best->Min, Corner))
			{
				Best = &Box;
			}
		}
		return Best;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelShiftSeamsLineTest,
	"PCGUtils.Voxel.ShiftSeams.SeamsMoveAndTheOutsideStays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelShiftSeamsLineTest::RunTest(const FString&)
{
	using namespace PCGVoxelShiftSeamsTests;

	// A row of four cells: three seams along X, every other face on the outside.
	const UPCGPointArrayData* Row = MakeVoxels(Block(4, 1, 1));
	const UPCGBasePointData* Out = Shift(MakeSettings(1.0f, 3), Row);
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	UTEST_EQUAL(TEXT("Same points"), Out->GetNumPoints(), 4);
	TArray<FBox> Boxes = BoxesOf(Out);
	Boxes.Sort([](const FBox& A, const FBox& B) { return A.Min.X < B.Min.X; });

	TestTrue(TEXT("The row still starts at -50"), FMath::IsNearlyEqual(Boxes[0].Min.X, -50.0, 1e-6));
	TestTrue(TEXT("The row still ends at 350"), FMath::IsNearlyEqual(Boxes[3].Max.X, 350.0, 1e-6));
	bool bTight = true, bMoved = false, bFlatSidesKept = true;
	for (int32 I = 0; I < 4; ++I)
	{
		if (I < 3)
		{
			bTight &= FMath::IsNearlyEqual(Boxes[I].Max.X, Boxes[I + 1].Min.X, 1e-6);
			bMoved |= !FMath::IsNearlyEqual(Boxes[I].Max.X, I * 100.0 + 50.0, 1e-3);
		}
		bFlatSidesKept &= Boxes[I].Min.Y == -50.0 && Boxes[I].Max.Y == 50.0 && Boxes[I].Min.Z == -50.0 && Boxes[I].Max.Z == 50.0;
		TestTrue(*FString::Printf(TEXT("Box %d keeps a positive width"), I), Boxes[I].Max.X - Boxes[I].Min.X > 1e-6);
	}
	TestTrue(TEXT("Neighbours still meet at every seam"), bTight);
	TestTrue(TEXT("At least one seam moved"), bMoved);
	TestTrue(TEXT("Faces on the outside do not move"), bFlatSidesKept);

	// Metadata is the input's.
	const FPCGMetadataAttribute<int32>* Id = static_cast<const FPCGMetadataAttribute<int32>*>(
		Out->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->GetConstAttribute(TEXT("Id")));
	UTEST_NOT_NULL(TEXT("Id kept"), Id);
	TestEqual(TEXT("Point 2 keeps Id 2"), Id->GetValueFromItemKey(Out->GetConstMetadataEntryValueRange()[2]), 2);

	// In a 2x2 grid each row's X seam can move on its own, but then the Y seam between the rows no longer has
	// matching sides and must stay: the low-left box still meets the box above it along its whole top.
	const UPCGBasePointData* Square = Shift(MakeSettings(1.0f, 5), MakeVoxels(Block(2, 2, 1)));
	UTEST_NOT_NULL(TEXT("Square ran"), Square);
	const TArray<FBox> Quad = BoxesOf(Square);
	const FBox* A = BoxAt(Quad, FVector(-50, -50, -50));
	const FBox* B = BoxAt(Quad, FVector(-50, 50, -50));
	UTEST_TRUE(TEXT("Boxes found"), A && B);
	const bool bRowsDiffer = !FMath::IsNearlyEqual(A->Max.X, B->Max.X, 1e-6);
	TestTrue(TEXT("The rows meet on one line"), FMath::IsNearlyEqual(A->Max.Y, B->Min.Y, 1e-6));
	TestTrue(TEXT("When the rows' X seams differ, the Y seam has stayed at 50"), !bRowsDiffer || FMath::IsNearlyEqual(A->Max.Y, 50.0, 1e-6));

	// Shifted seams are hidden: meshing with surface offsets from point bounds gives the same shell, even after two
	// full-strength passes have moved some seams further than half a cell.
	const UPCGBasePointData* Deep = Shift(MakeSettings(1.0f, 4), Shift(MakeSettings(1.0f, 3), Row));
	UTEST_NOT_NULL(TEXT("Two passes ran"), Deep);
	UPCGVoxelToDynMeshSettings* MeshSettings = NewObject<UPCGVoxelToDynMeshSettings>();
	MeshSettings->bConvertToLocalSpace = false;
	MeshSettings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;
	const UE::Geometry::FDynamicMesh3* Shell = MeshOf(Run<FPCGVoxelToDynMeshElement>(MeshSettings, { { PCGPinConstants::DefaultInputLabel, Deep } }));
	UTEST_NOT_NULL(TEXT("The shifted row meshes"), Shell);
	const FBox ShellBounds(Shell->GetBounds());
	TestTrue(*FString::Printf(TEXT("The shell is the row's (%s)"), *ShellBounds.ToString()),
		ShellBounds.Equals(FBox(FVector(-50, -50, -50), FVector(350, 50, 50)), 1e-3));

	// Strength 0 changes nothing; a bad noise scale is refused by name.
	const UPCGBasePointData* Still = Shift(MakeSettings(0.0f, 3), Row);
	UTEST_NOT_NULL(TEXT("Strength 0 ran"), Still);
	TestTrue(TEXT("Strength 0 leaves the bounds alone"), BoxesOf(Still) == BoxesOf(Row));
	UPCGVoxelShiftSeamsSettings* Noise = MakeSettings(0.5f, 3);
	Noise->Source = EPCGVoxelSeamShiftSource::Noise;
	Noise->NoiseScale.Constant = 0.0;
	AddExpectedError(TEXT("Noise Scale is 0"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("Noise Scale 0 gives no output"), Shift(Noise, Row));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelShiftSeamsFillTest,
	"PCGUtils.Voxel.ShiftSeams.BoxesStillFillExactlyTheSameSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelShiftSeamsFillTest::RunTest(const FString&)
{
	using namespace PCGVoxelShiftSeamsTests;

	for (int32 Trial = 0; Trial < 6; ++Trial)
	{
		// A random lumpy shape: 6x6x6 with about a third of the cells missing, so there are holes, notches and diagonals.
		FRandomStream Stream(100 + Trial);
		TArray<FIntVector> Cells;
		TSet<FIntVector> Filled;
		for (const FIntVector& Cell : Block(6, 6, 6))
		{
			if (Stream.FRand() < 0.67f)
			{
				Cells.Add(Cell);
				Filled.Add(Cell);
			}
		}
		const UPCGBasePointData* Tiled = CompressRandom(MakeVoxels(Cells), Trial);
		UTEST_NOT_NULL(TEXT("Compressed"), Tiled);

		// Random seams, then noise seams on top: chaining must stay valid too.
		const UPCGBasePointData* Once = Shift(MakeSettings(0.9f, Trial), Tiled);
		UTEST_NOT_NULL(TEXT("Shifted once"), Once);
		UPCGVoxelShiftSeamsSettings* NoiseSettings = MakeSettings(0.9f, Trial);
		NoiseSettings->Source = EPCGVoxelSeamShiftSource::Noise;
		NoiseSettings->NoiseScale.Constant = 250.0;
		const UPCGBasePointData* Twice = Shift(NoiseSettings, Once);
		UTEST_NOT_NULL(TEXT("Shifted twice"), Twice);

		for (const UPCGBasePointData* Result : { Once, Twice })
		{
			const TArray<FBox> Boxes = BoxesOf(Result);
			double Volume = 0.0;
			bool bPositive = true, bInside = true, bApart = true;
			int32 Moved = 0;
			const TArray<FBox> Before = BoxesOf(Tiled);
			for (int32 I = 0; I < Boxes.Num(); ++I)
			{
				const FVector Size = Boxes[I].GetSize();
				bPositive &= Size.X > 0.0 && Size.Y > 0.0 && Size.Z > 0.0;
				Volume += Size.X * Size.Y * Size.Z;
				Moved += Boxes[I].Equals(Before[I], 1e-3) ? 0 : 1;

				// Points just inside each corner must lie in filled cells: no box pokes out of the shape.
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const FVector T((Corner & 1) ? 0.995 : 0.005, (Corner & 2) ? 0.995 : 0.005, (Corner & 4) ? 0.995 : 0.005);
					const FVector P = Boxes[I].Min + Size * T;
					bInside &= Filled.Contains(FIntVector(FMath::RoundToInt(P.X / 100.0), FMath::RoundToInt(P.Y / 100.0), FMath::RoundToInt(P.Z / 100.0)));
				}
				for (int32 J = I + 1; J < Boxes.Num(); ++J)
				{
					const FBox Overlap = Boxes[I].Overlap(Boxes[J]);
					const FVector O = Overlap.GetSize();
					bApart &= !(Overlap.IsValid && O.X > 1e-6 && O.Y > 1e-6 && O.Z > 1e-6);
				}
			}
			const FString Which = FString::Printf(TEXT("trial %d, %s"), Trial, Result == Once ? TEXT("random") : TEXT("random then noise"));
			TestTrue(*FString::Printf(TEXT("Every box keeps a positive size (%s)"), *Which), bPositive);
			TestTrue(*FString::Printf(TEXT("No two boxes overlap (%s)"), *Which), bApart);
			TestTrue(*FString::Printf(TEXT("No box pokes out of the shape (%s)"), *Which), bInside);
			TestTrue(*FString::Printf(TEXT("Total volume equals the cells' volume (%s)"), *Which), FMath::IsNearlyEqual(Volume, Cells.Num() * 1.0e6, 1.0));
			TestTrue(*FString::Printf(TEXT("Some boxes changed (%s)"), *Which), Moved > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelShiftSeamsLimitTest,
	"PCGUtils.Voxel.ShiftSeams.ReachScalesWithChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelShiftSeamsLimitTest::RunTest(const FString&)
{
	using namespace PCGVoxelShiftSeamsTests;

	// The largest seam move over many seeds, at Strength 1, between a chunk spanning 4 along X and a second box.
	auto MaxMove = [](const FIntVector& SecondSpan, EPCGVoxelSeamLimit Limit)
	{
		const TArray<FIntVector> Spans = { FIntVector(4, 1, 1), SecondSpan };
		const UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0), FIntVector(4, 0, 0) }, 100.0, &Spans);
		double Largest = 0.0;
		for (int32 Seed = 1; Seed <= 24; ++Seed)
		{
			UPCGVoxelShiftSeamsSettings* Settings = MakeSettings(1.0f, Seed);
			Settings->Limit = Limit;
			if (const UPCGBasePointData* Out = Shift(Settings, Row))
			{
				if (const FBox* First = BoxAt(BoxesOf(Out), FVector(-50.0)))
				{
					Largest = FMath::Max(Largest, FMath::Abs(First->Max.X - 350.0));
				}
			}
		}
		return Largest;
	};

	// Two 4-cell chunks: the seam reaches past one cell (up to the full 4 cells at Strength 1).
	const double Chunks = MaxMove(FIntVector(4, 1, 1), EPCGVoxelSeamLimit::ThinnestEitherSide);
	TestTrue(FString::Printf(TEXT("chunk against chunk moves more than a cell (%.1f)"), Chunks), Chunks > 100.0 && Chunks <= 400.0 + 1e-6);

	// A chunk against a single cell: never more than that cell, whichever way it moves.
	const double AgainstCell = MaxMove(FIntVector(1), EPCGVoxelSeamLimit::ThinnestEitherSide);
	TestTrue(FString::Printf(TEXT("chunk against a cell moves at most a cell (%.1f)"), AgainstCell), AgainstCell > 0.0 && AgainstCell <= 100.0 + 1e-6);

	// The first version's limit: half the shrinking box, so at most 2 cells here.
	const double Old = MaxMove(FIntVector(4, 1, 1), EPCGVoxelSeamLimit::HalfShrinkingSide);
	TestTrue(FString::Printf(TEXT("old limit: at most half a chunk (%.1f)"), Old), Old > 0.0 && Old <= 200.0 + 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelShiftSeamsStrengthMaskTest,
	"PCGUtils.Voxel.ShiftSeams.StrengthPerBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelShiftSeamsStrengthMaskTest::RunTest(const FString&)
{
	using namespace PCGVoxelShiftSeamsTests;

	// Three 2-cell chunks in a row: seams at x = 150 and x = 350. A strength of 0 on the last chunk pins the second
	// seam; the first still moves for some seed.
	const TArray<FIntVector> Spans = { FIntVector(2, 1, 1), FIntVector(2, 1, 1), FIntVector(2, 1, 1) };
	UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0), FIntVector(2, 0, 0), FIntVector(4, 0, 0) }, 100.0, &Spans);
	FPCGMetadataAttribute<double>* Strength = Row->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)
		->CreateAttribute<double>(TEXT("SeamStrength"), 1.0, false);
	const TConstPCGValueRange<int64> Entries = Row->GetConstMetadataEntryValueRange();
	Strength->SetValue(Entries[0], 1.0);
	Strength->SetValue(Entries[1], 1.0);
	Strength->SetValue(Entries[2], 0.0);

	bool bFirstMoved = false;
	bool bSecondStill = true;
	for (int32 Seed = 1; Seed <= 12; ++Seed)
	{
		UPCGVoxelShiftSeamsSettings* Settings = MakeSettings(1.0f, Seed);
		Settings->Strength.Input = EPCGUtilsPointInputValueType::Attribute;
		const UPCGBasePointData* Out = Shift(Settings, Row);
		if (!Out)
		{
			continue;
		}
		const TArray<FBox> Boxes = BoxesOf(Out);
		const FBox* First = BoxAt(Boxes, FVector(-50, -50, -50));
		const FBox* Last = BoxAt(Boxes, FVector(350, -50, -50));
		bFirstMoved |= First && !FMath::IsNearlyEqual(First->Max.X, 150.0, 1e-6);
		bSecondStill &= Last && FMath::IsNearlyEqual(Last->Min.X, 350.0, 1e-6);
	}
	TestTrue(TEXT("the seam between full-strength chunks moves"), bFirstMoved);
	TestTrue(TEXT("a strength of 0 pins the seams of that chunk"), bSecondStill);

	// A strength outside 0..1 is refused, naming the point.
	Strength->SetValue(Entries[2], 2.0);
	UPCGVoxelShiftSeamsSettings* Bad = MakeSettings(1.0f, 1);
	Bad->Strength.Input = EPCGUtilsPointInputValueType::Attribute;
	AddExpectedErrorPlain(TEXT("Strength: 'SeamStrength' on point 2 is 2"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("bad strength gives no output"), Shift(Bad, Row));
	return true;
}

#endif
