// Copyright Max Harris
//
// Voxel | Extrude: growing and cutting along a direction, rounding, separate axes vs box, steps and values.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelExtrude.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelExtrudeTests
{
	using namespace PCGVoxelTestHelpers;

	struct FOutCell
	{
		FVector Min;
		FVector Max;
		int32 Id = -99;
		int32 Step = -99;
	};

	/** Runs the node; returns its cells by position (cells of 100), or an empty map when nothing came out. */
	TMap<FIntVector, FOutCell> Extrude(const UPCGData* In, FVector Direction, double Amount,
		TFunctionRef<void(UPCGVoxelExtrudeSettings&)> Edit = [](UPCGVoxelExtrudeSettings&) {})
	{
		UPCGVoxelExtrudeSettings* Settings = NewObject<UPCGVoxelExtrudeSettings>();
		Settings->Direction.Constant = Direction;
		Settings->Amount.Constant = Amount;
		Settings->bWriteExtrudeStep = true;
		Edit(*Settings);
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelExtrudeElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
		TMap<FIntVector, FOutCell> Result;
		const UPCGBasePointData* Points = Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
		if (!Points)
		{
			return Result;
		}
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* Id = static_cast<const FPCGMetadataAttribute<int32>*>(Domain->GetConstAttribute(TEXT("Id")));
		const FPCGMetadataAttribute<int32>* Step = static_cast<const FPCGMetadataAttribute<int32>*>(Domain->GetConstAttribute(Settings->ExtrudeStepAttribute));
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> Mins = Points->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> Maxs = Points->GetConstBoundsMaxValueRange();
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FVector P = Transforms[Index].GetLocation() / 100.0;
			FOutCell& Cell = Result.Add(FIntVector(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z)));
			Cell.Min = Mins[Index];
			Cell.Max = Maxs[Index];
			Cell.Id = Id ? Id->GetValueFromItemKey(Entries[Index]) : -99;
			Cell.Step = Step ? Step->GetValueFromItemKey(Entries[Index]) : -99;
		}
		return Result;
	}

	FString Describe(const TMap<FIntVector, FOutCell>& Cells)
	{
		TArray<FString> Parts;
		for (const TPair<FIntVector, FOutCell>& Pair : Cells)
		{
			Parts.Add(FString::Printf(TEXT("(%d %d %d) id %d step %d"), Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value.Id, Pair.Value.Step));
		}
		Parts.Sort();
		return FString::Join(Parts, TEXT(", "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelExtrudeGrowTest,
	"PCGUtils.Voxel.Extrude.Grow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelExtrudeGrowTest::RunTest(const FString&)
{
	using namespace PCGVoxelExtrudeTests;

	// Default: one cell up. The added cell copies its source's values but is a whole cell, even under a trimmed top.
	{
		UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0) });
		In->GetBoundsMaxValueRange()[0].Z = 20.0;
		const TMap<FIntVector, FOutCell> Cells = Extrude(In, FVector(0, 0, 1), 1.0);
		TestEqual(*FString::Printf(TEXT("Up 1: two cells (%s)"), *Describe(Cells)), Cells.Num(), 2);
		const FOutCell* Grown = Cells.Find(FIntVector(0, 0, 1));
		UTEST_NOT_NULL(TEXT("Up 1: the cell above was added"), Grown);
		TestEqual(TEXT("Up 1: it copies the source's Id"), Grown->Id, 0);
		TestEqual(TEXT("Up 1: it is step 1"), Grown->Step, 1);
		TestEqual(TEXT("Up 1: the source is step 0"), Cells.FindChecked(FIntVector(0, 0, 0)).Step, 0);
		TestTrue(TEXT("Up 1: the added cell is a whole cell"), Grown->Min.Equals(FVector(-50), 0.01) && Grown->Max.Equals(FVector(50), 0.01));
		TestTrue(TEXT("Up 1: the source keeps its own bounds"), FMath::IsNearlyEqual(Cells.FindChecked(FIntVector(0, 0, 0)).Max.Z, 20.0, 0.01));
	}
	// (3, 0, 0): three cells along +X, numbered 1, 2, 3.
	{
		const TMap<FIntVector, FOutCell> Cells = Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(3, 0, 0), 1.0);
		TestEqual(*FString::Printf(TEXT("+X 3: four cells (%s)"), *Describe(Cells)), Cells.Num(), 4);
		bool bSteps = true;
		for (int32 X = 1; X <= 3; ++X)
		{
			const FOutCell* Cell = Cells.Find(FIntVector(X, 0, 0));
			bSteps &= Cell && Cell->Step == X;
		}
		TestTrue(TEXT("+X 3: steps 1, 2, 3"), bSteps);
	}
	// Fractions round up; whole numbers stay; 0 does nothing.
	{
		TestEqual(TEXT("1.2 rounds up to 2 cells"), Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(1, 0, 0), 1.2).Num(), 3);
		TestEqual(TEXT("2.0 stays 2 cells"), Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(1, 0, 0), 2.0).Num(), 3);
		TestEqual(TEXT("Amount 0 does nothing"), Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(1, 0, 0), 0.0).Num(), 1);
		TestEqual(TEXT("Direction 0 does nothing"), Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(0, 0, 0), 5.0).Num(), 1);
		const TMap<FIntVector, FOutCell> Down = Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(0, 0, -1), 2.0);
		TestTrue(*FString::Printf(TEXT("(0, 0, -1) x 2 grows down (%s)"), *Describe(Down)),
			Down.Num() == 3 && Down.Contains(FIntVector(0, 0, -1)) && Down.Contains(FIntVector(0, 0, -2)));
	}
	// Separate axes: two runs. Off: the whole box.
	{
		const TMap<FIntVector, FOutCell> Runs = Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(2, 0, 2), 1.0);
		TestEqual(*FString::Printf(TEXT("Separate axes: 1 + 2 + 2 cells (%s)"), *Describe(Runs)), Runs.Num(), 5);
		const TMap<FIntVector, FOutCell> Box = Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(2, 0, 2), 1.0,
			[](UPCGVoxelExtrudeSettings& S) { S.bSeparateAxes = false; });
		TestEqual(*FString::Printf(TEXT("Box: 3 x 3 cells (%s)"), *Describe(Box)), Box.Num(), 9);
		const FOutCell* Corner = Box.Find(FIntVector(2, 0, 1));
		TestTrue(TEXT("Box: a cell's step is its largest axis distance"), Corner && Corner->Step == 2);
	}
	// Filled cells stay; where two cells grow into one, the smallest step wins.
	{
		const TMap<FIntVector, FOutCell> Cells = Extrude(MakeVoxels({ FIntVector(0, 0, 0), FIntVector(0, 0, 2) }), FVector(0, 0, 1), 3.0);
		TestEqual(*FString::Printf(TEXT("Overlap: cells 0 to 5 (%s)"), *Describe(Cells)), Cells.Num(), 6);
		const FOutCell* Shared = Cells.Find(FIntVector(0, 0, 3));
		TestTrue(TEXT("Overlap: cell 3 comes from the nearer cell (Id 1, step 1)"), Shared && Shared->Id == 1 && Shared->Step == 1);
		const FOutCell* Kept = Cells.Find(FIntVector(0, 0, 2));
		TestTrue(TEXT("Overlap: a filled cell stays itself (Id 1, step 0)"), Kept && Kept->Id == 1 && Kept->Step == 0);
	}
	// Added Values None: added cells get attribute defaults.
	{
		const TMap<FIntVector, FOutCell> Cells = Extrude(MakeVoxels({ FIntVector(0, 0, 0) }), FVector(0, 0, 1), 1.0,
			[](UPCGVoxelExtrudeSettings& S) { S.AddedValues = EPCGVoxelOffsetAddedValues::None; });
		const FOutCell* Grown = Cells.Find(FIntVector(0, 0, 1));
		TestTrue(TEXT("Added Values None: the added cell has the default Id"), Grown && Grown->Id == -1);
		TestEqual(TEXT("Added Values None: the source keeps its Id"), Cells.FindChecked(FIntVector(0, 0, 0)).Id, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelExtrudeRemoveTest,
	"PCGUtils.Voxel.Extrude.Remove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelExtrudeRemoveTest::RunTest(const FString&)
{
	using namespace PCGVoxelExtrudeTests;

	// A column of four; only the bottom cell has Amount -1 (per-point attribute): it and the cell above go.
	UPCGPointArrayData* In = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2), FIntVector(0, 0, 3) });
	FPCGMetadataDomain* Domain = In->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	FPCGMetadataAttribute<double>* Amount = Domain->CreateAttribute<double>(TEXT("ExtrudeAmount"), 0.0, false);
	Amount->SetValue(In->GetMetadataEntryValueRange()[0], -1.0);
	const TMap<FIntVector, FOutCell> Cells = Extrude(In, FVector(0, 0, 1), 0.0,
		[](UPCGVoxelExtrudeSettings& S) { S.Amount.SetAttribute(TEXT("ExtrudeAmount")); });
	TestTrue(*FString::Printf(TEXT("-1 removes the cell and the one above (%s)"), *Describe(Cells)),
		Cells.Num() == 2 && Cells.Contains(FIntVector(0, 0, 2)) && Cells.Contains(FIntVector(0, 0, 3)));

	// Removal wins over a neighbour's growth into the same cell.
	// Along -X: cell 3 (Amount 2) grows into 2 (filled) and 1; cell 2 (Amount -1) removes itself and 1. Only 3 is left.
	UPCGPointArrayData* Pair = MakeVoxels({ FIntVector(2, 0, 0), FIntVector(3, 0, 0) });
	FPCGMetadataDomain* PairDomain = Pair->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	FPCGMetadataAttribute<double>* PairAmount = PairDomain->CreateAttribute<double>(TEXT("ExtrudeAmount"), 0.0, false);
	PairAmount->SetValue(Pair->GetMetadataEntryValueRange()[0], -1.0);
	PairAmount->SetValue(Pair->GetMetadataEntryValueRange()[1], 2.0);
	const TMap<FIntVector, FOutCell> Mixed = Extrude(Pair, FVector(-1, 0, 0), 0.0,
		[](UPCGVoxelExtrudeSettings& S) { S.Amount.SetAttribute(TEXT("ExtrudeAmount")); });
	TestTrue(*FString::Printf(TEXT("Removal wins over growth into the same cell (%s)"), *Describe(Mixed)),
		Mixed.Num() == 1 && Mixed.Contains(FIntVector(3, 0, 0)));
	return true;
}

#endif
