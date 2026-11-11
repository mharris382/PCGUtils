// Copyright Max Harris
//
// Voxel | Compress Random: size modes (Up To, Exact, Cubes Up To), the chance to compress, and both read per start cell.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelCompress.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelCompressRandomTests
{
	using namespace PCGVoxelTestHelpers;

	/** A block of equal cells (the per-point Id removed, so every cell may merge with every other). */
	UPCGPointArrayData* EqualBlock(int32 X, int32 Y, int32 Z)
	{
		UPCGPointArrayData* Points = MakeVoxels(Block(X, Y, Z));
		Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)->DeleteAttribute(TEXT("Id"));
		return Points;
	}

	template <typename T>
	void AddPerCell(UPCGPointArrayData* Points, FName Name, TFunctionRef<T(const FIntVector&)> Value)
	{
		FPCGMetadataAttribute<T>* Attribute = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)->CreateAttribute<T>(Name, T{}, false);
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FVector P = Transforms[Index].GetLocation() / 100.0;
			Attribute->SetValue(Entries[Index], Value(FIntVector(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z))));
		}
	}

	/** The boxes (first cell, span) of a Random compress, or empty when it failed. */
	TArray<TPair<FIntVector, FIntVector>> Compress(const UPCGData* In, TFunctionRef<void(UPCGVoxelCompressSettings&)> Edit)
	{
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->Shape = EPCGVoxelChunkShape::Random;
		Settings->Seed = 11;
		Edit(*Settings);
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
		TArray<TPair<FIntVector, FIntVector>> Boxes;
		const UPCGBasePointData* Points = Out.Num() == 1 ? Cast<UPCGBasePointData>(Out[0].Data) : nullptr;
		if (!Points)
		{
			return Boxes;
		}
		FPCGContext Context;
		FPCGVoxelGrid Grid;
		if (FPCGVoxelGrid::Build(&Context, Points, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Grid))
		{
			for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
			{
				Boxes.Emplace(Grid.PointFirst[Point], Grid.PointSpan[Point]);
			}
		}
		return Boxes;
	}

	int32 CountSpan(const TArray<TPair<FIntVector, FIntVector>>& Boxes, const FIntVector& Span)
	{
		return Boxes.FilterByPredicate([&](const TPair<FIntVector, FIntVector>& Box) { return Box.Value == Span; }).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCompressRandomModesTest, "PCGUtils.Voxel.Chunks.RandomSizeModesAndChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCompressRandomModesTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelCompressRandomTests;
	using FBoxes = TArray<TPair<FIntVector, FIntVector>>;

	// Exact 2x2x2 on a 6x6x6 of equal cells: 27 cubes of 2, nothing else.
	const FBoxes Twos = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Constant = FVector(2.0);
	});
	TestTrue(TEXT("Exact 2: 27 boxes, all 2x2x2"), Twos.Num() == 27 && CountSpan(Twos, FIntVector(2)) == 27);

	// Exact 4: the box at the origin fits; nothing else can, so the rest stay single cells (never a smaller box).
	const FBoxes Fours = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Constant = FVector(4.0);
	});
	TestEqual(TEXT("Exact 4: one 4-cube"), CountSpan(Fours, FIntVector(4)), 1);
	TestEqual(TEXT("Exact 4: every other box a single cell"), CountSpan(Fours, FIntVector(1)), Fours.Num() - 1);
	TestEqual(TEXT("Exact 4: 1 + 216 - 64 boxes"), Fours.Num(), 1 + 216 - 64);

	// Bricks: exactly 3x1x1.
	const FBoxes Bricks = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Constant = FVector(3.0, 1.0, 1.0);
	});
	TestTrue(TEXT("Exact 3x1x1: 72 bricks"), Bricks.Num() == 72 && CountSpan(Bricks, FIntVector(3, 1, 1)) == 72);

	// Cubes Up To 3: every box a cube of side 1 to 3, and at least one bigger than a cell.
	const FBoxes Cubes = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Cubes;
		S.RandomSpan.Constant = FVector(3.0);
	});
	bool bAllCubes = !Cubes.IsEmpty();
	bool bAnyBig = false;
	for (const TPair<FIntVector, FIntVector>& Box : Cubes)
	{
		bAllCubes &= Box.Value.X == Box.Value.Y && Box.Value.Y == Box.Value.Z && Box.Value.X >= 1 && Box.Value.X <= 3;
		bAnyBig |= Box.Value.X > 1;
	}
	TestTrue(TEXT("Cubes Up To 3: only cubes up to 3"), bAllCubes);
	TestTrue(TEXT("Cubes Up To 3: some bigger than a cell"), bAnyBig);

	// Chance 0: nothing compresses. Chance 0.5 with Exact 2: some cubes, some loose cells.
	const FBoxes None = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S) { S.RandomChance.Constant = 0.0; });
	TestTrue(TEXT("Chance 0: 216 single cells"), None.Num() == 216 && CountSpan(None, FIntVector(1)) == 216);
	const FBoxes Half = Compress(EqualBlock(6, 6, 6), [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Constant = FVector(2.0);
		S.RandomChance.Constant = 0.5;
	});
	const int32 HalfCubes = CountSpan(Half, FIntVector(2));
	TestTrue(TEXT("Chance 0.5: some cubes and some loose cells, nothing else"),
		HalfCubes > 0 && HalfCubes < 27 && HalfCubes + CountSpan(Half, FIntVector(1)) == Half.Num());

	// Chance per start cell: 1 where X < 3, else 0. Only the low half compresses.
	UPCGPointArrayData* Masked = EqualBlock(6, 6, 6);
	AddPerCell<double>(Masked, TEXT("CompressChance"), [](const FIntVector& C) { return C.X < 3 ? 1.0 : 0.0; });
	const FBoxes MaskedBoxes = Compress(Masked, [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Constant = FVector(3.0);
		S.RandomChance.Input = EPCGUtilsPointInputValueType::Attribute;
	});
	bool bOnlyLow = true;
	for (const TPair<FIntVector, FIntVector>& Box : MaskedBoxes)
	{
		bOnlyLow &= Box.Value == FIntVector(1) || Box.Key.X < 3;
	}
	TestTrue(TEXT("per-cell chance: boxes start only where the chance is 1"), bOnlyLow);
	TestEqual(TEXT("per-cell chance: the low half is 4 cubes of 3"), CountSpan(MaskedBoxes, FIntVector(3)), 4);

	// Span per start cell: (2,2,2) everywhere from the attribute is the same as the constant.
	UPCGPointArrayData* Sized = EqualBlock(6, 6, 6);
	AddPerCell<FVector>(Sized, TEXT("CompressSpan"), [](const FIntVector&) { return FVector(2.0); });
	const FBoxes SizedBoxes = Compress(Sized, [](UPCGVoxelCompressSettings& S)
	{
		S.RandomSize = EPCGVoxelRandomSize::Exact;
		S.RandomSpan.Input = EPCGUtilsPointInputValueType::Attribute;
	});
	TestEqual(TEXT("per-cell span: 27 cubes of 2"), CountSpan(SizedBoxes, FIntVector(2)), 27);

	// A span that is not whole cells is refused, naming the point.
	UPCGPointArrayData* BadSpan = EqualBlock(2, 1, 1);
	AddPerCell<FVector>(BadSpan, TEXT("CompressSpan"), [](const FIntVector& C) { return C.X == 0 ? FVector(1.5, 1.0, 1.0) : FVector(1.0); });
	AddExpectedErrorPlain(TEXT("Random Span: 'CompressSpan' on point 0 is"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("bad span stops the input"),
		Compress(BadSpan, [](UPCGVoxelCompressSettings& S) { S.RandomSpan.Input = EPCGUtilsPointInputValueType::Attribute; }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCompressMaxSpanDataTest, "PCGUtils.Voxel.Chunks.MaxSpanFromData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCompressMaxSpanDataTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelCompressRandomTests;

	// Max Span from @Data: 2 on a 4x4x4 of equal cells gives 8 cubes of 2; 4 gives one cube.
	auto Cubes = [](int32 DataSpan)
	{
		UPCGPointArrayData* In = EqualBlock(4, 4, 4);
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(In), TEXT("CompressMaxSpan"), DataSpan);
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->MaxSpan.Input = EPCGUtilsInputValueType::Attribute;
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
		const UPCGBasePointData* Points = Out.Num() == 1 ? Cast<UPCGBasePointData>(Out[0].Data) : nullptr;
		return Points ? Points->GetNumPoints() : -1;
	};
	TestEqual(TEXT("@Data Max Span 2: 8 cubes"), Cubes(2), 8);
	TestEqual(TEXT("@Data Max Span 4: 1 cube"), Cubes(4), 1);
	AddExpectedErrorPlain(TEXT("Max Span is 0; it must be at least 1"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("@Data Max Span 0 is refused"), Cubes(0), -1);
	return true;
}

#endif
