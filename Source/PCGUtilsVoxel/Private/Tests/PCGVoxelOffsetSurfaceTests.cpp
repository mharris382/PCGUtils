// Copyright Max Harris
//
// Voxel | Offset Surface: whole cells add or remove layers, a growing fraction is part cells (each box inside its own
// cell, never overlapping), a shrinking fraction moves open faces, inside corners split.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelOffsetSurface.h"
#include "Elements/PCGVoxelToDynMesh.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelOffsetSurfaceTests
{
	using namespace PCGVoxelTestHelpers;

	struct FOutPoint
	{
		FIntVector Cell;
		FVector Min;
		FVector Max;
		int32 Id;
	};

	/** Runs the node and reads its one output pin. */
	TArray<FOutPoint> Offset(const UPCGData* In, double Amount, TFunctionRef<void(UPCGVoxelOffsetSurfaceSettings&)> Edit,
		TArray<FPCGTaggedData>* OutRaw = nullptr)
	{
		UPCGVoxelOffsetSurfaceSettings* Settings = NewObject<UPCGVoxelOffsetSurfaceSettings>();
		Settings->Amount.Constant = Amount;
		Edit(*Settings);
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelOffsetSurfaceElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } })
			.FilterByPredicate([](const FPCGTaggedData& Data) { return Data.Pin == PCGPinConstants::DefaultOutputLabel; });
		if (OutRaw)
		{
			*OutRaw = Outputs;
		}
		TArray<FOutPoint> Result;
		const UPCGBasePointData* Points = Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
		if (!Points)
		{
			return Result;
		}
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* Id = Domain ? static_cast<const FPCGMetadataAttribute<int32>*>(Domain->GetConstAttribute(TEXT("Id"))) : nullptr;
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> Mins = Points->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> Maxs = Points->GetConstBoundsMaxValueRange();
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FVector P = Transforms[Index].GetLocation() / 100.0;
			Result.Add({ FIntVector(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z)),
				Mins[Index], Maxs[Index], Id ? Id->GetValueFromItemKey(Entries[Index]) : -99 });
		}
		return Result;
	}

	TArray<FOutPoint> Offset(const UPCGData* In, double Amount)
	{
		return Offset(In, Amount, [](UPCGVoxelOffsetSurfaceSettings&) {});
	}

	/** Per point of the single dataset in Raw, the Boolean attribute Name (empty when missing). */
	TArray<bool> Bools(const TArray<FPCGTaggedData>& Raw, FName Name)
	{
		TArray<bool> Values;
		const UPCGBasePointData* Points = Raw.Num() == 1 ? Cast<UPCGBasePointData>(Raw[0].Data) : nullptr;
		const FPCGMetadataDomain* Domain = Points ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const auto* Attribute = Domain ? static_cast<const FPCGMetadataAttribute<bool>*>(Domain->GetConstAttribute(Name)) : nullptr;
		if (Attribute)
		{
			const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
			for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
			{
				Values.Add(Attribute->GetValueFromItemKey(Entries[Index]));
			}
		}
		return Values;
	}

	const FOutPoint* At(const TArray<FOutPoint>& Points, const FIntVector& Cell)
	{
		return Points.FindByPredicate([&](const FOutPoint& P) { return P.Cell == Cell; });
	}

	int32 CountAt(const TArray<FOutPoint>& Points, const FIntVector& Cell)
	{
		return Points.FilterByPredicate([&](const FOutPoint& P) { return P.Cell == Cell; }).Num();
	}

	void AddDoubles(UPCGBasePointData* Points, FName Name, const TArray<double>& Values)
	{
		FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<double>* Attribute = Domain->CreateAttribute<double>(Name, 1.0, false);
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			Attribute->SetValue(Entries[Index], Values[Index]);
		}
	}

	bool BoundsAre(const FOutPoint* P, const FVector& Min, const FVector& Max)
	{
		return P && P->Min.Equals(Min, 1e-6) && P->Max.Equals(Max, 1e-6);
	}

	FBox WorldBox(const FOutPoint& P)
	{
		const FVector Centre = FVector(P.Cell) * 100.0;
		return FBox(Centre + P.Min, Centre + P.Max);
	}

	/** The box around every point's box, in world space. */
	FBox Around(const TArray<FOutPoint>& Points)
	{
		FBox Box(ForceInit);
		for (const FOutPoint& P : Points)
		{
			Box += WorldBox(P);
		}
		return Box;
	}

	/** True when every box stays inside its own cell (cells of 100). */
	bool InsideCells(const TArray<FOutPoint>& Points)
	{
		return !Points.ContainsByPredicate([](const FOutPoint& P)
		{
			return P.Min.GetMin() < -50.0 - 1e-6 || P.Max.GetMax() > 50.0 + 1e-6;
		});
	}

	/** Pairs of boxes that overlap by more than a sliver. */
	int32 Overlaps(const TArray<FOutPoint>& Points)
	{
		int32 Count = 0;
		for (int32 A = 0; A < Points.Num(); ++A)
		{
			const FBox BoxA = WorldBox(Points[A]);
			for (int32 B = A + 1; B < Points.Num(); ++B)
			{
				const FBox BoxB = WorldBox(Points[B]);
				const FVector Lo = BoxA.Min.ComponentMax(BoxB.Min);
				const FVector Hi = BoxA.Max.ComponentMin(BoxB.Max);
				if ((Hi - Lo).GetMin() > 1e-3)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	/** Every box inside its cell, none overlapping, the box around them Min..Max (world). */
	void TestGrown(FAutomationTestBase& Test, const TCHAR* What, const TArray<FOutPoint>& Points, const FVector& Min, const FVector& Max)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: every box inside its cell"), What), InsideCells(Points));
		Test.TestEqual(FString::Printf(TEXT("%s: no overlapping boxes"), What), Overlaps(Points), 0);
		const FBox Box = Around(Points);
		Test.TestTrue(FString::Printf(TEXT("%s: reaches %s to %s (got %s to %s)"), What, *Min.ToString(), *Max.ToString(), *Box.Min.ToString(), *Box.Max.ToString()),
			Box.Min.Equals(Min, 1e-6) && Box.Max.Equals(Max, 1e-6));
	}

	double Volume(const TArray<FOutPoint>& Points)
	{
		double Sum = 0.0;
		for (const FOutPoint& P : Points)
		{
			const FVector Size = P.Max - P.Min;
			Sum += Size.X * Size.Y * Size.Z;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceFractionTest,
	"PCGUtils.Voxel.OffsetSurface.Fraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceFractionTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// One cell grows a quarter cell on every side: the cell stays whole, its 26 neighbours get the quarter (6 slabs,
	// 12 edge bars, 8 corner cubes), each inside its own cell. Every box copies the cell (Id 0); the tag is kept.
	TArray<FPCGTaggedData> Raw;
	const TArray<FOutPoint> Grown = Offset(MakeVoxels({ FIntVector(0) }), 0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
	TestEqual(TEXT("one cell grown: itself and 26 part cells"), Grown.Num(), 27);
	TestGrown(*this, TEXT("+0.25"), Grown, FVector(-75.0), FVector(75.0));
	TestNearlyEqual(TEXT("+0.25 volume"), Volume(Grown), 1.5 * 1.5 * 1.5 * 1e6, 1.0);
	TestTrue(TEXT("the cell stays whole"), BoundsAre(At(Grown, FIntVector(0)), FVector(-50.0), FVector(50.0)));
	TestTrue(TEXT("+X slab against the cell"), BoundsAre(At(Grown, FIntVector(1, 0, 0)), FVector(-50.0), FVector(-25.0, 50.0, 50.0)));
	TestTrue(TEXT("corner cube against the cell"), BoundsAre(At(Grown, FIntVector(-1, -1, -1)), FVector(25.0), FVector(50.0)));
	TestTrue(TEXT("every box copies the cell"), !Grown.ContainsByPredicate([](const FOutPoint& P) { return P.Id != 0; }));
	TestTrue(TEXT("tag kept"), Raw.Num() == 1 && Raw[0].Tags.Contains(TEXT("KeptTag")));

	// A 3x3x1 plate shrinks: the middle only on Z, a corner on -X, -Y and Z.
	const TArray<FOutPoint> Shrunk = Offset(MakeVoxels(Block(3, 3, 1)), -0.25);
	TestEqual(TEXT("plate keeps 9 points"), Shrunk.Num(), 9);
	if (const FOutPoint* Middle = At(Shrunk, FIntVector(1, 1, 0)))
	{
		TestTrue(TEXT("middle shrinks on Z only"), Middle->Min.Equals(FVector(-50, -50, -25), 1e-6) && Middle->Max.Equals(FVector(50, 50, 25), 1e-6));
	}
	if (const FOutPoint* Corner = At(Shrunk, FIntVector(0, 0, 0)))
	{
		TestTrue(TEXT("corner shrinks on its open sides"), Corner->Min.Equals(FVector(-25, -25, -25), 1e-6) && Corner->Max.Equals(FVector(50, 50, 25), 1e-6));
	}

	// More than half a cell off both opposite sides leaves nothing of a lone cell.
	TestEqual(TEXT("-0.5 removes a lone cell"), Offset(MakeVoxels({ FIntVector(0) }), -0.5).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceCornersTest,
	"PCGUtils.Voxel.OffsetSurface.InnerCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceCornersTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// An L of three cells: the corner cell (0,0) has the empty (1,1) diagonal. Shrinking a quarter splits it in two
	// (an L in XY), the arms stay one box each.
	const TArray<FIntVector> L = { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) };
	const TArray<FOutPoint> Split = Offset(MakeVoxels(L), -0.25);
	TestEqual(TEXT("split: 4 points"), Split.Num(), 4);
	TestEqual(TEXT("corner cell in 2 boxes"), CountAt(Split, FIntVector(0, 0, 0)), 2);
	TestEqual(TEXT("arm in 1 box"), CountAt(Split, FIntVector(1, 0, 0)), 1);
	// Corner: (0.75^2 - 0.25^2) * 0.5; each arm 0.75 * 0.5 * 0.5; in cubic cells of 100.
	TestNearlyEqual(TEXT("split volume is the exact inset"), Volume(Split), (0.25 + 2.0 * 0.1875) * 1e6, 1.0);
	for (const FOutPoint& P : Split)
	{
		if (P.Cell == FIntVector(0, 0, 0))
		{
			TestTrue(TEXT("no corner box reaches into the notch"), P.Max.X <= 25.0 + 1e-6 || P.Max.Y <= 25.0 + 1e-6);
		}
	}

	const TArray<FOutPoint> NoSplit = Offset(MakeVoxels(L), -0.25, [](UPCGVoxelOffsetSurfaceSettings& S) { S.bSplitInnerCorners = false; });
	TestEqual(TEXT("no split: 3 points"), NoSplit.Num(), 3);
	TestNearlyEqual(TEXT("no split keeps the notch filled"), Volume(NoSplit), (0.75 * 0.75 * 0.5 + 2.0 * 0.1875) * 1e6, 1.0);

	// A cell with every neighbour filled except one diagonal corner (inside a 3x3x3 block missing a corner) loses a
	// cube of a quarter cell: 3 boxes.
	TArray<FIntVector> Notched = Block(3, 3, 3);
	Notched.Remove(FIntVector(2, 2, 2));
	const TArray<FOutPoint> Carved = Offset(MakeVoxels(Notched), -0.25);
	TestEqual(TEXT("middle cell carved into 3 boxes"), CountAt(Carved, FIntVector(1, 1, 1)), 3);
	double Middle = 0.0;
	for (const FOutPoint& P : Carved)
	{
		if (P.Cell == FIntVector(1, 1, 1))
		{
			const FVector Size = P.Max - P.Min;
			Middle += Size.X * Size.Y * Size.Z;
		}
	}
	TestNearlyEqual(TEXT("middle cell volume"), Middle, (1.0 - 0.25 * 0.25 * 0.25) * 1e6, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceLayersTest,
	"PCGUtils.Voxel.OffsetSurface.WholeCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceLayersTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// 5x5x5, -1: the inner 3x3x3 stays. Delete keeps each cell's own Id; Push gives the new surface the old one's.
	auto IdOf = [](const FIntVector& C) { return C.X + 5 * C.Y + 25 * C.Z; };
	const TArray<FOutPoint> Deleted = Offset(MakeVoxels(Block(5, 5, 5)), -1.0);
	TestEqual(TEXT("-1 leaves 27 cells"), Deleted.Num(), 27);
	if (const FOutPoint* P = At(Deleted, FIntVector(1, 2, 2)))
	{
		TestEqual(TEXT("Delete: own Id"), P->Id, IdOf(FIntVector(1, 2, 2)));
		TestTrue(TEXT("whole cell bounds"), P->Min.Equals(FVector(-50.0), 1e-6) && P->Max.Equals(FVector(50.0), 1e-6));
	}
	TestNull(TEXT("outer layer gone"), At(Deleted, FIntVector(0, 2, 2)));

	const TArray<FOutPoint> Pushed = Offset(MakeVoxels(Block(5, 5, 5)), -1.0,
		[](UPCGVoxelOffsetSurfaceSettings& S) { S.RemovedCells = EPCGVoxelOffsetRemovedCells::Push; });
	TestEqual(TEXT("Push: same 27 cells"), Pushed.Num(), 27);
	if (const FOutPoint* P = At(Pushed, FIntVector(1, 2, 2)))
	{
		TestEqual(TEXT("Push: new surface takes the old surface's Id"), P->Id, IdOf(FIntVector(0, 2, 2)));
	}
	if (const FOutPoint* P = At(Pushed, FIntVector(2, 2, 2)))
	{
		TestEqual(TEXT("Push: the inside keeps its own"), P->Id, IdOf(FIntVector(2, 2, 2)));
	}

	// -1.25 on the 5x5x5: 27 cells, the outer ones pulled in a quarter.
	const TArray<FOutPoint> Deeper = Offset(MakeVoxels(Block(5, 5, 5)), -1.25);
	TestEqual(TEXT("-1.25 keeps 27 cells"), Deeper.Num(), 27);
	TestNearlyEqual(TEXT("-1.25 volume is a 2.5 cube"), Deeper.Num() ? Volume(Deeper) : 0.0, 2.5 * 2.5 * 2.5 * 1e6, 1.0);

	// One cell, +1.5: a whole 3x3x3 block, then a layer of cells half deep (5x5x5 cells in all); added cells copy the
	// nearest (Id 0). No box leaves its cell.
	const TArray<FOutPoint> Grown = Offset(MakeVoxels({ FIntVector(0) }), 1.5);
	TestEqual(TEXT("+1.5 gives 5x5x5 cells"), Grown.Num(), 125);
	TestNearlyEqual(TEXT("+1.5 volume is a 4 cube"), Volume(Grown), 4.0 * 4.0 * 4.0 * 1e6, 1.0);
	TestGrown(*this, TEXT("+1.5"), Grown, FVector(-200.0), FVector(200.0));
	if (const FOutPoint* P = At(Grown, FIntVector(-1, -1, -1)))
	{
		TestTrue(TEXT("the whole layer is whole cells"), BoundsAre(P, FVector(-50.0), FVector(50.0)));
		TestEqual(TEXT("added cell copies the nearest"), P->Id, 0);
	}
	TestTrue(TEXT("the outer corner is half a cell deep"), BoundsAre(At(Grown, FIntVector(-2, -2, -2)), FVector(0.0), FVector(50.0)));
	const TArray<FOutPoint> Plain = Offset(MakeVoxels({ FIntVector(0) }), 1.0,
		[](UPCGVoxelOffsetSurfaceSettings& S) { S.AddedValues = EPCGVoxelOffsetAddedValues::None; });
	if (const FOutPoint* P = At(Plain, FIntVector(1, 0, 0)))
	{
		TestEqual(TEXT("Added Values None: default Id"), P->Id, -1);
	}
	if (const FOutPoint* P = At(Plain, FIntVector(0, 0, 0)))
	{
		TestEqual(TEXT("Added Values None: the input cell keeps its Id"), P->Id, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceChunksTest,
	"PCGUtils.Voxel.OffsetSurface.ChunksAndErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceChunksTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// A 2x1x1 chunk comes out as its two cells, without the span attribute, plus the part cells around them (4x3x3 in all).
	const TArray<FIntVector> Spans = { FIntVector(2, 1, 1) };
	TArray<FPCGTaggedData> Raw;
	const TArray<FOutPoint> Expanded = Offset(MakeVoxels({ FIntVector(0) }, 100.0, &Spans), 0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
	TestEqual(TEXT("chunk expanded: 2 cells and 34 part cells"), Expanded.Num(), 36);
	TestTrue(TEXT("each chunk cell is whole"), BoundsAre(At(Expanded, FIntVector(0, 0, 0)), FVector(-50.0), FVector(50.0))
		&& BoundsAre(At(Expanded, FIntVector(1, 0, 0)), FVector(-50.0), FVector(50.0)));
	TestGrown(*this, TEXT("chunk +0.25"), Expanded, FVector(-75.0), FVector(175.0, 75.0, 75.0));
	const UPCGBasePointData* Points = Raw.Num() == 1 ? Cast<UPCGBasePointData>(Raw[0].Data) : nullptr;
	TestTrue(TEXT("no span attribute"), Points && !Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->HasAttribute(TEXT("VoxelSpan")));

	AddExpectedErrorPlain(TEXT("Max Cells"), EAutomationExpectedErrorFlags::Contains, 1);
	const TArray<FOutPoint> TooBig = Offset(MakeVoxels(Block(4, 4, 4)), 2.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.MaxCells = 100; });
	TestEqual(TEXT("Max Cells stops the input"), TooBig.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceScaleTest,
	"PCGUtils.Voxel.OffsetSurface.UnitsAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceScaleTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// Absolute: 25 world units on 100 cells is a quarter cell.
	const TArray<FOutPoint> Absolute = Offset(MakeVoxels({ FIntVector(0) }), 25.0,
		[](UPCGVoxelOffsetSurfaceSettings& S) { S.Units = EPCGVoxelOffsetUnits::Absolute; });
	TestEqual(TEXT("Absolute 25: 27 cells"), Absolute.Num(), 27);
	TestGrown(*this, TEXT("Absolute 25 = a quarter cell"), Absolute, FVector(-75.0), FVector(75.0));

	// Constant scale multiplies.
	const TArray<FOutPoint> Doubled = Offset(MakeVoxels({ FIntVector(0) }), 0.25,
		[](UPCGVoxelOffsetSurfaceSettings& S) { S.AmountScale.Constant = 2.0; });
	TestEqual(TEXT("Amount Scale 2: 27 cells"), Doubled.Num(), 27);
	TestGrown(*this, TEXT("Amount Scale 2"), Doubled, FVector(-100.0), FVector(100.0));

	// Per cell: a row scaled 0, 1, -1. The first stays, the middle grows (whole, with 24 part cells around it), the last
	// shrinks on its open sides (a part cell next to it does not hold its face).
	auto Attribute = [](UPCGVoxelOffsetSurfaceSettings& S) { S.AmountScale.Input = EPCGUtilsPointInputValueType::Attribute; };
	UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0) });
	AddDoubles(Row, TEXT("AmountScale"), { 0.0, 1.0, -1.0 });
	const TArray<FOutPoint> Mixed = Offset(Row, 0.25, Attribute);
	TestEqual(TEXT("mixed: 3 cells and 24 part cells"), Mixed.Num(), 27);
	TestEqual(TEXT("mixed: no overlapping boxes"), Overlaps(Mixed), 0);
	TestTrue(TEXT("scale 0 leaves the cell alone"), BoundsAre(At(Mixed, FIntVector(0, 0, 0)), FVector(-50.0), FVector(50.0)));
	TestTrue(TEXT("scale 1 stays whole"), BoundsAre(At(Mixed, FIntVector(1, 0, 0)), FVector(-50.0), FVector(50.0)));
	TestTrue(TEXT("scale 1 grows a quarter into +Y"), BoundsAre(At(Mixed, FIntVector(1, 1, 0)), FVector(-50.0), FVector(50.0, -25.0, 50.0)));
	TestTrue(TEXT("scale -1 shrinks its open sides"), BoundsAre(At(Mixed, FIntVector(2, 0, 0)), FVector(-50, -25, -25), FVector(25, 25, 25)));

	// Per cell whole layers: only the cell scaled 1 grows a layer.
	UPCGPointArrayData* Pair = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
	AddDoubles(Pair, TEXT("AmountScale"), { 1.0, 0.0 });
	const TArray<FOutPoint> OneGrows = Offset(Pair, 1.0, Attribute);
	TestEqual(TEXT("one cell's 3x3x3 plus the other"), OneGrows.Num(), 27);
	TestNull(TEXT("the unscaled cell grows nothing"), At(OneGrows, FIntVector(2, 0, 0)));
	if (const FOutPoint* P = At(OneGrows, FIntVector(-1, 1, 1)))
	{
		TestEqual(TEXT("grown cell copies the cell that grew it"), P->Id, 0);
	}

	// @Data: one scale for the whole set.
	UPCGPointArrayData* Single = MakeVoxels({ FIntVector(0) });
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Single), TEXT("AmountScale"), -1.0);
	const TArray<FOutPoint> FromData = Offset(Single, 0.25, [](UPCGVoxelOffsetSurfaceSettings& S)
	{
		S.AmountScale.SetAttribute(TEXT("@Data.AmountScale"));
	});
	TestTrue(TEXT("@Data scale -1 shrinks"), FromData.Num() == 1 && BoundsAre(&FromData[0], FVector(-25.0), FVector(25.0)));

	// A missing attribute is an error naming the setting.
	AddExpectedErrorPlain(TEXT("Amount Scale: attribute 'AmountScale' was not found on the points"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("missing attribute stops the input"), Offset(MakeVoxels({ FIntVector(0) }), 0.25, Attribute).Num(), 0);

	// ---- Amount itself per point ----
	// A row with amounts 0, 0.25, -0.25: the same result as scaling 0.25 by 0, 1, -1.
	UPCGPointArrayData* AmountRow = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0) });
	AddDoubles(AmountRow, TEXT("OffsetAmount"), { 0.0, 0.25, -0.25 });
	const TArray<FOutPoint> PerPoint = Offset(AmountRow, 99.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Amount.Input = EPCGUtilsPointInputValueType::Attribute; });
	TestEqual(TEXT("per-point amount: 27 points"), PerPoint.Num(), 27);
	TestTrue(TEXT("amount 0 leaves the cell alone"), BoundsAre(At(PerPoint, FIntVector(0, 0, 0)), FVector(-50.0), FVector(50.0)));
	TestTrue(TEXT("amount 0.25 grows a quarter into +Y"), BoundsAre(At(PerPoint, FIntVector(1, 1, 0)), FVector(-50.0), FVector(50.0, -25.0, 50.0)));
	TestTrue(TEXT("amount -0.25 shrinks its open sides"), BoundsAre(At(PerPoint, FIntVector(2, 0, 0)), FVector(-50, -25, -25), FVector(25, 25, 25)));

	// Both per point multiply: 0.5 x 0.5 is a quarter.
	UPCGPointArrayData* Both = MakeVoxels({ FIntVector(0) });
	AddDoubles(Both, TEXT("OffsetAmount"), { 0.5 });
	AddDoubles(Both, TEXT("AmountScale"), { 0.5 });
	const TArray<FOutPoint> Multiplied = Offset(Both, 99.0, [](UPCGVoxelOffsetSurfaceSettings& S)
	{
		S.Amount.Input = EPCGUtilsPointInputValueType::Attribute;
		S.AmountScale.Input = EPCGUtilsPointInputValueType::Attribute;
	});
	TestGrown(*this, TEXT("0.5 x 0.5 grows a quarter"), Multiplied, FVector(-75.0), FVector(75.0));

	// A property works too: $Density is 1 on every point, scaled by a constant quarter.
	const TArray<FOutPoint> FromDensity = Offset(MakeVoxels({ FIntVector(0) }), 99.0, [](UPCGVoxelOffsetSurfaceSettings& S)
	{
		S.Amount.SetAttribute(TEXT("$Density"));
		S.AmountScale.Constant = 0.25;
	});
	TestGrown(*this, TEXT("$Density x 0.25 grows a quarter"), FromDensity, FVector(-75.0), FVector(75.0));

	// A value that is not finite names the point.
	UPCGPointArrayData* Bad = MakeVoxels({ FIntVector(0), FIntVector(1, 0, 0) });
	AddDoubles(Bad, TEXT("OffsetAmount"), { 0.25, std::numeric_limits<double>::quiet_NaN() });
	AddExpectedErrorPlain(TEXT("Amount: 'OffsetAmount' on point 1 is"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("NaN amount stops the input"),
		Offset(Bad, 0.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Amount.Input = EPCGUtilsPointInputValueType::Attribute; }).Num(), 0);

	// A text attribute is the wrong type.
	UPCGPointArrayData* Text = MakeVoxels({ FIntVector(0) });
	Text->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements)->CreateAttribute<FString>(TEXT("OffsetAmount"), TEXT("x"), false);
	AddExpectedErrorPlain(TEXT("this setting needs a number"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("text amount stops the input"),
		Offset(Text, 0.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Amount.Input = EPCGUtilsPointInputValueType::Attribute; }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceOverrideLabelTest, "PCGUtils.Voxel.OffsetSurface.OverridePinLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelOffsetSurfaceOverrideLabelTest::RunTest(const FString& Parameters)
{
	// Amount and AmountScale were plain numbers; their override pins keep those labels as point shorthands.
	TSet<FName> Labels;
	for (const FPCGSettingsOverridableParam& Param : NewObject<UPCGVoxelOffsetSurfaceSettings>()->OverridableParams())
	{
		Labels.Add(Param.Label);
	}
	for (const TCHAR* Expected : { TEXT("Amount"), TEXT("AmountAttribute"), TEXT("AmountScale"), TEXT("AmountScaleAttribute") })
	{
		TestTrue(FString::Printf(TEXT("override pin '%s'"), Expected), Labels.Contains(FName(Expected)));
	}
	TestFalse(TEXT("no bare Constant pin"), Labels.Contains(TEXT("Constant")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfacePinsTest, "PCGUtils.Voxel.OffsetSurface.SplitCellsOnOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfacePinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	TestEqual(TEXT("one output pin"), NewObject<UPCGVoxelOffsetSurfaceSettings>()->DefaultOutputPinProperties().Num(), 1);

	// An L shrunk a quarter: Out has the corner's two carved boxes (both flagged, largest first) plus the arms.
	const TArray<FIntVector> L = { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) };
	TArray<FPCGTaggedData> GridRaw;
	const TArray<FOutPoint> Grid = Offset(MakeVoxels(L), -0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &GridRaw);
	TestEqual(TEXT("Out: the corner's 2 boxes plus the arms"), Grid.Num(), 4);
	const TArray<bool> Split = Bools(GridRaw, TEXT("WasSplit"));
	if (TestEqual(TEXT("Out: WasSplit on every point"), Split.Num(), 4))
	{
		for (int32 Index = 0; Index < Grid.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("Out: WasSplit of %s"), *Grid[Index].Cell.ToString()), Split[Index], Grid[Index].Cell == FIntVector(0, 0, 0));
		}
	}

	if (GridRaw.Num() == 1)
	{
		// Out feeds another voxel node: the corner reads as one filled cell.
		TestEqual(TEXT("Out feeds another voxel node"), Offset(GridRaw[0].Data, 0.0).Num(), 3);

		// The cell's points read as the box around them: the open-face box (-25..50, -25..50, -25..25).
		FPCGVoxelGrid Read;
		const UPCGBasePointData* OutPoints = Cast<UPCGBasePointData>(GridRaw[0].Data);
		if (TestTrue(TEXT("Out reads as a voxel set"), FPCGVoxelGrid::Build(nullptr, OutPoints, FPCGVoxelGridReadSettings(), FText::FromString(TEXT("Voxel Test")), 0, Read)))
		{
			TArray<FVector> MinSide, MaxSide;
			Read.ComputeSurfaceOffsets(OutPoints, MinSide, MaxSide);
			const int32 Owner = Read.FindPoint(FIntVector(0));
			TestTrue(TEXT("the corner reads as the box around its pieces"), Owner != INDEX_NONE
				&& MinSide[Owner].Equals(FVector(-25.0), 1e-6) && MaxSide[Owner].Equals(FVector(0.0, 0.0, -25.0), 1e-6));
		}

		// Meshed (Exact offsets): the exact shape of the carved boxes, notch left open. The corner reads as its face
		// box, and the arms' walls close the notch: corner (0.75^2 - 0.25^2) x 0.5, each arm 0.75 x 0.5 x 0.5, cells of 100.
		UPCGVoxelToDynMeshSettings* MeshSettings = NewObject<UPCGVoxelToDynMeshSettings>();
		MeshSettings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;
		const UE::Geometry::FDynamicMesh3* Mesh = MeshOf(Run<FPCGVoxelToDynMeshElement>(MeshSettings, { { PCGPinConstants::DefaultInputLabel, GridRaw[0].Data } }));
		if (TestNotNull(TEXT("Out meshes"), Mesh))
		{
			TestEqual(TEXT("mesh is closed"), OpenEdges(*Mesh), 0);
			TestNearlyEqual(TEXT("mesh volume is the carved boxes"), PCGVoxelTestHelpers::Volume(*Mesh), (0.25 + 2.0 * 0.1875) * 1e6, 1.0);
		}
	}

	// A notch inside a 3x3x3: the middle cell (all six neighbours filled) is its 3 carved boxes, flagged and visible.
	TArray<FIntVector> Notched = Block(3, 3, 3);
	Notched.Remove(FIntVector(2, 2, 2));
	const TArray<FOutPoint> NotchedGrid = Offset(MakeVoxels(Notched), -0.25);
	TestEqual(TEXT("notched Out: the middle in 3 boxes"), CountAt(NotchedGrid, FIntVector(1, 1, 1)), 3);
	// The three cells touching the notch along an edge split in 2: 26 cells, +2 for the middle, +3 for those.
	TestEqual(TEXT("notched Out: 31 points"), NotchedGrid.Num(), 31);

	// Hidden cells stay on Out (filter on Is Surrounded to spawn only visible ones): 125 cells and the 7x7x7 shell.
	TestEqual(TEXT("5x5x5 grown a quarter: 7x7x7 points"), Offset(MakeVoxels(Block(5, 5, 5)), 0.25).Num(), 343);

	// IsSurrounded on the input is rewritten for the offset cells: -1 on 5x5x5 leaves 27, only the middle hidden.
	UPCGPointArrayData* WithFlag = MakeVoxels(Block(5, 5, 5));
	{
		FPCGMetadataAttribute<bool>* Flag = WithFlag->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)
			->CreateAttribute<bool>(TEXT("IsSurrounded"), false, false);
		const TConstPCGValueRange<int64> Entries = WithFlag->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < WithFlag->GetNumPoints(); ++Index)
		{
			Flag->SetValue(Entries[Index], true); // stale: everything claims to be hidden
		}
	}
	TArray<FPCGTaggedData> FlagRaw;
	Offset(WithFlag, -1.0, [](UPCGVoxelOffsetSurfaceSettings&) {}, &FlagRaw);
	int32 Hidden = 0;
	const TArray<bool> Surrounded = Bools(FlagRaw, TEXT("IsSurrounded"));
	for (const bool bHidden : Surrounded)
	{
		Hidden += bHidden ? 1 : 0;
	}
	TestTrue(TEXT("Out: 27 cells, 1 surrounded"), Surrounded.Num() == 27 && Hidden == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceAxesTest, "PCGUtils.Voxel.OffsetSurface.Axes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceAxesTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;
	const int32 XY = static_cast<int32>(EPCGVoxelAxes::X | EPCGVoxelAxes::Y);
	auto OnlyXY = [XY](UPCGVoxelOffsetSurfaceSettings& S) { S.Axes = XY; };

	TestEqual(TEXT("default: X, Y and Z"), NewObject<UPCGVoxelOffsetSurfaceSettings>()->Axes, static_cast<int32>(EPCGVoxelAxes::All));

	// A fraction grows only into X and Y: the cell and its 8 neighbours in the plane.
	const TArray<FOutPoint> Faces = Offset(MakeVoxels({ FIntVector(0) }), 0.25, OnlyXY);
	TestEqual(TEXT("X | Y: +0.25 is 9 points"), Faces.Num(), 9);
	TestGrown(*this, TEXT("X | Y: +0.25 grows sideways only"), Faces, FVector(-75, -75, -50), FVector(75, 75, 50));

	// Whole layers grow sideways only: one cell +1 is a 3x3 plate.
	const TArray<FOutPoint> Plate = Offset(MakeVoxels({ FIntVector(0) }), 1.0, OnlyXY);
	TestEqual(TEXT("X | Y: +1 gives a 3x3 plate"), Plate.Num(), 9);
	TestTrue(TEXT("X | Y: nothing above or below"), !Plate.ContainsByPredicate([](const FOutPoint& P) { return P.Cell.Z != 0; }));

	// A shrink by a layer peels the sides, not the top and bottom: a 3x3x3 keeps its middle column.
	const TArray<FOutPoint> Column = Offset(MakeVoxels(Block(3, 3, 3)), -1.0, OnlyXY);
	TestEqual(TEXT("X | Y: -1 on 3x3x3 keeps the middle column"), Column.Num(), 3);
	TestTrue(TEXT("X | Y: the column is the middle"), !Column.ContainsByPredicate([](const FOutPoint& P) { return P.Cell.X != 1 || P.Cell.Y != 1; }));

	// Inside corners carve only along X and Y: the L corner splits into 2 full-height boxes.
	const TArray<FIntVector> L = { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) };
	const TArray<FOutPoint> Corner = Offset(MakeVoxels(L), -0.25, OnlyXY);
	TestEqual(TEXT("X | Y: corner in 2 boxes"), CountAt(Corner, FIntVector(0, 0, 0)), 2);
	TestTrue(TEXT("X | Y: every box keeps its full height"), !Corner.ContainsByPredicate([](const FOutPoint& P)
	{
		return !FMath::IsNearlyEqual(P.Min.Z, -50.0, 1e-6) || !FMath::IsNearlyEqual(P.Max.Z, 50.0, 1e-6);
	}));
	// Corner (0.75^2 - 0.25^2) x 1, each arm 0.75 x 0.5 x 1; in cells of 100.
	TestNearlyEqual(TEXT("X | Y: corner volume"), Volume(Corner), (0.5 + 2.0 * 0.375) * 1e6, 1.0);

	// Only Z: a cell grows up and down.
	const TArray<FOutPoint> Tall = Offset(MakeVoxels({ FIntVector(0) }), 1.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Axes = static_cast<int32>(EPCGVoxelAxes::Z); });
	TestEqual(TEXT("Z: +1 gives a column of 3"), Tall.Num(), 3);

	// No axis: nothing moves.
	const TArray<FOutPoint> Still = Offset(MakeVoxels({ FIntVector(0) }), 1.5, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Axes = 0; });
	TestTrue(TEXT("no axis: the cell is left as it is"), Still.Num() == 1 && BoundsAre(&Still[0], FVector(-50.0), FVector(50.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceNoOverlapTest, "PCGUtils.Voxel.OffsetSurface.GrowNeverOverlaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceNoOverlapTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;

	// An L grown a quarter: the inside corner cell (1,1,0) is reached by both arms and the corner cell. It is the union
	// of their parts, an L of 2 boxes (both flagged Was Split, as are the L cells above and below it), and nothing
	// overlaps (the old bug).
	const TArray<FIntVector> L = { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) };
	TArray<FPCGTaggedData> Raw;
	const TArray<FOutPoint> Grown = Offset(MakeVoxels(L), 0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
	TestGrown(*this, TEXT("L +0.25"), Grown, FVector(-75.0), FVector(175.0, 175.0, 75.0));
	TestEqual(TEXT("inside corner: 2 boxes"), CountAt(Grown, FIntVector(1, 1, 0)), 2);
	double Corner = 0.0;
	for (const FOutPoint& P : Grown)
	{
		if (P.Cell == FIntVector(1, 1, 0))
		{
			const FVector Size = P.Max - P.Min;
			Corner += Size.X * Size.Y * Size.Z;
			TestTrue(TEXT("inside corner: no box reaches past a quarter on both X and Y"), P.Max.X <= -25.0 + 1e-6 || P.Max.Y <= -25.0 + 1e-6);
		}
	}
	TestNearlyEqual(TEXT("inside corner: an L, 1 x 0.25 + 0.25 x 0.75"), Corner, (0.25 + 0.1875) * 1e6, 1.0);
	// The exact box offset: in plan 3 squares of 1.5 (6.75) less their overlaps (0.75 + 0.75 + 0.25) plus the triple
	// (0.25) = 5.25, times 1.5 high.
	TestNearlyEqual(TEXT("L +0.25: the exact box offset"), Volume(Grown), 5.25 * 1.5 * 1e6, 1.0);
	const TArray<bool> Split = Bools(Raw, TEXT("WasSplit"));
	if (TestEqual(TEXT("WasSplit on every point"), Split.Num(), Grown.Num()))
	{
		for (int32 Index = 0; Index < Grown.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("WasSplit of %s"), *Grown[Index].Cell.ToString()), Split[Index], Grown[Index].Cell.X == 1 && Grown[Index].Cell.Y == 1);
		}
	}

	// Cells that touch only along an edge: each grows its corner into the two cells between them; no overlap.
	const TArray<FOutPoint> Diagonal = Offset(MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 1, 0) }), 0.25);
	TestEqual(TEXT("diagonal pair: no overlapping boxes"), Overlaps(Diagonal), 0);
	TestTrue(TEXT("diagonal pair: every box inside its cell"), InsideCells(Diagonal));

	// A one-cell gap, each side growing 0.75: the gap is filled, as 2 boxes that meet, each from its own side.
	UPCGPointArrayData* Gap = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(2, 0, 0) });
	const TArray<FOutPoint> Closed = Offset(Gap, 0.75);
	TestEqual(TEXT("gap: no overlapping boxes"), Overlaps(Closed), 0);
	TestEqual(TEXT("gap: 2 boxes"), CountAt(Closed, FIntVector(1, 0, 0)), 2);
	double GapVolume = 0.0;
	for (const FOutPoint& P : Closed)
	{
		if (P.Cell == FIntVector(1, 0, 0))
		{
			const FVector Size = P.Max - P.Min;
			GapVolume += Size.X * Size.Y * Size.Z;
			TestEqual(TEXT("gap: each box copies the side it touches"), P.Id, P.Min.X < -49.0 ? 0 : 1);
		}
	}
	TestNearlyEqual(TEXT("gap: filled exactly once"), GapVolume, 1e6, 1.0);

	// Different amounts per cell (the case in the field): a 4x4 plate, amounts 0.3 to 1.8, never overlaps.
	TArray<FIntVector> Plate = Block(4, 4, 1);
	UPCGPointArrayData* Varied = MakeVoxels(Plate);
	TArray<double> Amounts;
	for (int32 Index = 0; Index < Plate.Num(); ++Index)
	{
		Amounts.Add(0.3 + 0.1 * ((Index * 7) % 16));
	}
	AddDoubles(Varied, TEXT("OffsetAmount"), Amounts);
	const TArray<FOutPoint> Mixed = Offset(Varied, 0.0, [](UPCGVoxelOffsetSurfaceSettings& S) { S.Amount.Input = EPCGUtilsPointInputValueType::Attribute; });
	TestTrue(TEXT("varied: grew"), Mixed.Num() > Plate.Num());
	TestTrue(TEXT("varied: every box inside its cell"), InsideCells(Mixed));
	TestEqual(TEXT("varied: no overlapping boxes"), Overlaps(Mixed), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelOffsetSurfaceGrowMeshTest, "PCGUtils.Voxel.OffsetSurface.GrowMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelOffsetSurfaceGrowMeshTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelOffsetSurfaceTests;
	// Out meshes as the exact shape of its boxes (Surface Offset From Point Bounds), part cells and grown corners included.
	auto Probe = [&](const TCHAR* What, const TArray<FIntVector>& Cells, double Amount)
	{
		TArray<FPCGTaggedData> Raw;
		const TArray<FOutPoint> Pts = Offset(MakeVoxels(Cells), Amount, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
		UPCGVoxelToDynMeshSettings* MeshSettings = NewObject<UPCGVoxelToDynMeshSettings>();
		MeshSettings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;
		const UE::Geometry::FDynamicMesh3* Mesh = Raw.Num() == 1 ? MeshOf(Run<FPCGVoxelToDynMeshElement>(MeshSettings, { { PCGPinConstants::DefaultInputLabel, Raw[0].Data } })) : nullptr;
		if (TestNotNull(FString::Printf(TEXT("%s meshes"), What), Mesh))
		{
			TestEqual(FString::Printf(TEXT("%s: mesh is closed"), What), OpenEdges(*Mesh), 0);
			TestNearlyEqual(FString::Printf(TEXT("%s: mesh volume is the boxes'"), What), PCGVoxelTestHelpers::Volume(*Mesh), Volume(Pts), 1.0);
		}
	};
	Probe(TEXT("cell +0.25"), { FIntVector(0) }, 0.25);
	Probe(TEXT("cell +1.5"), { FIntVector(0) }, 1.5);
	Probe(TEXT("L +0.25"), { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) }, 0.25);
	return true;
}

#endif
