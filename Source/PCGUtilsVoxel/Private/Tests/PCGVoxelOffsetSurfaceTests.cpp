// Copyright Max Harris
//
// Voxel | Offset Surface: fractions move open faces (bounds), whole cells add or remove layers, inside corners split.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelOffsetSurface.h"
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

	TArray<FOutPoint> Offset(const UPCGData* In, double Amount, TFunctionRef<void(UPCGVoxelOffsetSurfaceSettings&)> Edit,
		TArray<FPCGTaggedData>* OutRaw = nullptr)
	{
		UPCGVoxelOffsetSurfaceSettings* Settings = NewObject<UPCGVoxelOffsetSurfaceSettings>();
		Settings->Amount = Amount;
		Edit(*Settings);
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelOffsetSurfaceElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
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

	const FOutPoint* At(const TArray<FOutPoint>& Points, const FIntVector& Cell)
	{
		return Points.FindByPredicate([&](const FOutPoint& P) { return P.Cell == Cell; });
	}

	int32 CountAt(const TArray<FOutPoint>& Points, const FIntVector& Cell)
	{
		return Points.FilterByPredicate([&](const FOutPoint& P) { return P.Cell == Cell; }).Num();
	}

	void AddScales(UPCGBasePointData* Points, const TArray<double>& Values)
	{
		FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<double>* Attribute = Domain->CreateAttribute<double>(TEXT("AmountScale"), 1.0, false);
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

	// One cell grows a quarter cell on every side; keeps its Id and tag.
	TArray<FPCGTaggedData> Raw;
	const TArray<FOutPoint> Grown = Offset(MakeVoxels({ FIntVector(0) }), 0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
	if (TestEqual(TEXT("one cell, one point"), Grown.Num(), 1))
	{
		TestTrue(TEXT("grown bounds"), Grown[0].Min.Equals(FVector(-75.0), 1e-6) && Grown[0].Max.Equals(FVector(75.0), 1e-6));
		TestEqual(TEXT("Id kept"), Grown[0].Id, 0);
		TestTrue(TEXT("tag kept"), Raw.Num() == 1 && Raw[0].Tags.Contains(TEXT("KeptTag")));
	}

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

	// One cell, +1.5: a 3x3x3 block, the outer faces half a cell further; added cells copy the nearest (Id 0).
	const TArray<FOutPoint> Grown = Offset(MakeVoxels({ FIntVector(0) }), 1.5);
	TestEqual(TEXT("+1.5 gives 27 cells"), Grown.Num(), 27);
	TestNearlyEqual(TEXT("+1.5 volume is a 4 cube"), Volume(Grown), 4.0 * 4.0 * 4.0 * 1e6, 1.0);
	if (const FOutPoint* P = At(Grown, FIntVector(-1, -1, -1)))
	{
		TestTrue(TEXT("corner cell grows on its 3 open sides"), P->Min.Equals(FVector(-100.0), 1e-6) && P->Max.Equals(FVector(50.0), 1e-6));
		TestEqual(TEXT("added cell copies the nearest"), P->Id, 0);
	}
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

	// A 2x1x1 chunk comes out as two cells, without the span attribute, each grown on its open sides only.
	const TArray<FIntVector> Spans = { FIntVector(2, 1, 1) };
	TArray<FPCGTaggedData> Raw;
	const TArray<FOutPoint> Expanded = Offset(MakeVoxels({ FIntVector(0) }, 100.0, &Spans), 0.25, [](UPCGVoxelOffsetSurfaceSettings&) {}, &Raw);
	TestEqual(TEXT("chunk expanded to 2 cells"), Expanded.Num(), 2);
	if (const FOutPoint* P = At(Expanded, FIntVector(0, 0, 0)))
	{
		TestTrue(TEXT("shared face stays put"), FMath::IsNearlyEqual(P->Max.X, 50.0, 1e-6) && FMath::IsNearlyEqual(P->Min.X, -75.0, 1e-6));
	}
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
	TestTrue(TEXT("Absolute 25 = a quarter cell"), Absolute.Num() == 1 && BoundsAre(&Absolute[0], FVector(-75.0), FVector(75.0)));

	// Constant scale multiplies.
	const TArray<FOutPoint> Doubled = Offset(MakeVoxels({ FIntVector(0) }), 0.25,
		[](UPCGVoxelOffsetSurfaceSettings& S) { S.AmountScale = 2.0; });
	TestTrue(TEXT("Amount Scale 2"), Doubled.Num() == 1 && BoundsAre(&Doubled[0], FVector(-100.0), FVector(100.0)));

	// Per cell: a row scaled 0, 1, -1. The first stays, the middle grows on its open sides, the last shrinks.
	auto Attribute = [](UPCGVoxelOffsetSurfaceSettings& S) { S.AmountScaleInput = EPCGVoxelOffsetScaleInput::Attribute; };
	UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0) });
	AddScales(Row, { 0.0, 1.0, -1.0 });
	const TArray<FOutPoint> Mixed = Offset(Row, 0.25, Attribute);
	TestEqual(TEXT("mixed: 3 points"), Mixed.Num(), 3);
	TestTrue(TEXT("scale 0 leaves the cell alone"), BoundsAre(At(Mixed, FIntVector(0, 0, 0)), FVector(-50.0), FVector(50.0)));
	TestTrue(TEXT("scale 1 grows its open sides"), BoundsAre(At(Mixed, FIntVector(1, 0, 0)), FVector(-50, -75, -75), FVector(50, 75, 75)));
	TestTrue(TEXT("scale -1 shrinks its open sides"), BoundsAre(At(Mixed, FIntVector(2, 0, 0)), FVector(-50, -25, -25), FVector(25, 25, 25)));

	// Per cell whole layers: only the cell scaled 1 grows a layer.
	UPCGPointArrayData* Pair = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
	AddScales(Pair, { 1.0, 0.0 });
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
		S.AmountScaleInput = EPCGVoxelOffsetScaleInput::Attribute;
		S.AmountScaleAttribute.Update(TEXT("@Data.AmountScale"));
	});
	TestTrue(TEXT("@Data scale -1 shrinks"), FromData.Num() == 1 && BoundsAre(&FromData[0], FVector(-25.0), FVector(25.0)));

	// A missing attribute is an error naming the setting.
	AddExpectedErrorPlain(TEXT("Amount Scale Attribute"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("missing attribute stops the input"), Offset(MakeVoxels({ FIntVector(0) }), 0.25, Attribute).Num(), 0);
	return true;
}

#endif
