// Copyright Max Harris
//
// Chunks: Compress merges only equal cells, Expand gives the cells back, and To DynMesh cannot tell the difference.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelCompress.h"
#include "Elements/PCGVoxelExpand.h"
#include "Elements/PCGVoxelToDynMesh.h"
#include "Helpers/PCGHelpers.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelChunkTests
{
	using namespace PCGVoxelTestHelpers;

	const UPCGBasePointData* Compress(const UPCGData* Voxels, EPCGVoxelChunkShape Shape = EPCGVoxelChunkShape::Cubes,
		EPCGVoxelMatchAttributes Match = EPCGVoxelMatchAttributes::All, TArray<FName> Listed = {})
	{
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->Shape = Shape;
		Settings->MatchAttributes = Match;
		Settings->ListedAttributes = Listed;
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
	}

	const UPCGBasePointData* Expand(const UPCGData* Voxels)
	{
		UPCGVoxelExpandSettings* Settings = NewObject<UPCGVoxelExpandSettings>();
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelExpandElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
	}

	TArray<FPCGTaggedData> ToMesh(const UPCGData* Voxels, bool bColors)
	{
		UPCGVoxelToDynMeshSettings* Settings = NewObject<UPCGVoxelToDynMeshSettings>();
		Settings->bConvertToLocalSpace = false;
		Settings->bCollapseCoplanarFaces = false;
		if (bColors)
		{
			Settings->VertexColorSource = EPCGVoxelColorSource::Attribute;
			Settings->ColorTransition = EPCGVoxelColorTransition::Smooth;
		}
		return Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
	}

	/** A 4x4x4 block whose 'Kind' attribute is 1 except in one corner cell, and whose colors split in two halves. */
	UPCGPointArrayData* MakeBlock()
	{
		UPCGPointArrayData* Voxels = MakeVoxels(Block(4, 4, 4));
		FPCGMetadataDomain* Elements = Voxels->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		Elements->DeleteAttribute(TEXT("Id")); // per-cell ids would make every cell different
		FPCGMetadataAttribute<int32>* Kind = Elements->CreateAttribute<int32>(TEXT("Kind"), 1, false);
		auto Keys = Voxels->GetMetadataEntryValueRange();
		auto Transforms = Voxels->GetTransformValueRange();
		auto Colors = Voxels->GetColorValueRange();
		for (int32 I = 0; I < Voxels->GetNumPoints(); ++I)
		{
			const FVector P = Transforms[I].GetLocation();
			Kind->SetValue(Keys[I], P.Equals(FVector(300, 300, 300)) ? 2 : 1);
			Colors[I] = P.Z < 200 ? FVector4(1, 0, 0, 1) : FVector4(0, 1, 0, 1);
		}
		return Voxels;
	}

	int32 KindAt(const UPCGBasePointData* Points, int32 Index)
	{
		const FPCGMetadataAttribute<int32>* Kind = static_cast<const FPCGMetadataAttribute<int32>*>(
			Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->GetConstAttribute(TEXT("Kind")));
		return Kind ? Kind->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]) : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelChunkMeshTest,
	"PCGUtils.Voxel.Chunks.MeshIsTheSameCompressedOrNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelChunkMeshTest::RunTest(const FString&)
{
	using namespace PCGVoxelChunkTests;

	const UPCGPointArrayData* Plain = MakeBlock();
	for (const EPCGVoxelChunkShape Shape : { EPCGVoxelChunkShape::Cubes, EPCGVoxelChunkShape::Boxes })
	{
		const TCHAR* Name = Shape == EPCGVoxelChunkShape::Cubes ? TEXT("Cubes") : TEXT("Boxes");
		const UPCGBasePointData* Compressed = Compress(Plain, Shape);
		UTEST_NOT_NULL(*FString::Printf(TEXT("%s: compressed"), Name), Compressed);
		TestTrue(*FString::Printf(TEXT("%s: fewer points (%d of 64)"), Name, Compressed->GetNumPoints()), Compressed->GetNumPoints() < 64);

		const TArray<FPCGTaggedData> PlainOut = ToMesh(Plain, true);
		const TArray<FPCGTaggedData> ChunkOut = ToMesh(Compressed, true);
		const UE::Geometry::FDynamicMesh3* PlainMesh = MeshOf(PlainOut);
		const UE::Geometry::FDynamicMesh3* ChunkMesh = MeshOf(ChunkOut);
		UTEST_TRUE(*FString::Printf(TEXT("%s: both meshed"), Name), PlainMesh && ChunkMesh);
		TestEqual(*FString::Printf(TEXT("%s: same triangle count"), Name), ChunkMesh->TriangleCount(), PlainMesh->TriangleCount());
		TestTrue(*FString::Printf(TEXT("%s: same vertex positions"), Name), SortedPositions(*ChunkMesh) == SortedPositions(*PlainMesh));

		TArray<FVector4f> PlainColors, ChunkColors;
		for (const int32 E : PlainMesh->Attributes()->PrimaryColors()->ElementIndicesItr()) PlainColors.Add(PlainMesh->Attributes()->PrimaryColors()->GetElement(E));
		for (const int32 E : ChunkMesh->Attributes()->PrimaryColors()->ElementIndicesItr()) ChunkColors.Add(ChunkMesh->Attributes()->PrimaryColors()->GetElement(E));
		auto ByValue = [](const FVector4f& A, const FVector4f& B) { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : (A.Z != B.Z ? A.Z < B.Z : A.W < B.W)); };
		PlainColors.Sort(ByValue);
		ChunkColors.Sort(ByValue);
		TestTrue(*FString::Printf(TEXT("%s: same smooth colors (cells are counted, not points)"), Name), PlainColors == ChunkColors);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelChunkRoundTripTest,
	"PCGUtils.Voxel.Chunks.ExpandGivesBackTheCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelChunkRoundTripTest::RunTest(const FString&)
{
	using namespace PCGVoxelChunkTests;

	const UPCGPointArrayData* Plain = MakeBlock();
	const UPCGBasePointData* Compressed = Compress(Plain);
	UTEST_NOT_NULL(TEXT("Compressed"), Compressed);
	const UPCGBasePointData* Expanded = Expand(Compressed);
	UTEST_NOT_NULL(TEXT("Expanded"), Expanded);
	UTEST_EQUAL(TEXT("64 cells again"), Expanded->GetNumPoints(), 64);

	FPCGContext Context;
	FPCGVoxelGrid Before, After;
	UTEST_TRUE(TEXT("Original reads"), FPCGVoxelGrid::Build(&Context, Plain, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Before));
	UTEST_TRUE(TEXT("Round trip reads"), FPCGVoxelGrid::Build(&Context, Expanded, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, After));
	TestFalse(TEXT("The span attribute is gone"), After.bChunked);
	bool bSame = Before.Cells == After.Cells;
	for (const FIntVector& Cell : Before.Cells)
	{
		bSame &= KindAt(Plain, Before.FindPoint(Cell)) == KindAt(Expanded, After.FindPoint(Cell));
		bSame &= Plain->GetConstColorValueRange()[Before.FindPoint(Cell)] == Expanded->GetConstColorValueRange()[After.FindPoint(Cell)];
	}
	TestTrue(TEXT("Every cell is back with its own Kind and color"), bSame);
	TestEqual(TEXT("Expanded cells are seeded like Volume Sampler cells"),
		Expanded->GetConstSeedValueRange()[After.FindPoint(FIntVector(1, 0, 0))], PCGHelpers::ComputeSeed(1, 0, 0));

	const UPCGBasePointData* PassThrough = Expand(Plain);
	TestTrue(TEXT("Expanding a set without chunks returns it unchanged"), PassThrough == Plain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelChunkEqualityTest,
	"PCGUtils.Voxel.Chunks.OnlyEqualCellsMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelChunkEqualityTest::RunTest(const FString&)
{
	using namespace PCGVoxelChunkTests;

	// A 2x2x2 block whose last cell has a different Kind: All keeps eight cells, Listed without Kind merges them.
	UPCGPointArrayData* Voxels = MakeVoxels(Block(2, 2, 2));
	FPCGMetadataDomain* Elements = Voxels->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	Elements->DeleteAttribute(TEXT("Id"));
	FPCGMetadataAttribute<int32>* Kind = Elements->CreateAttribute<int32>(TEXT("Kind"), 1, false);
	Kind->SetValue(Voxels->GetMetadataEntryValueRange()[7], 5);

	const UPCGBasePointData* All = Compress(Voxels);
	UTEST_NOT_NULL(TEXT("All compressed"), All);
	TestEqual(TEXT("A cell that differs in one attribute stays out of the chunk"), All->GetNumPoints(), 8);

	const UPCGBasePointData* Listed = Compress(Voxels, EPCGVoxelChunkShape::Cubes, EPCGVoxelMatchAttributes::Listed, { TEXT("$Color") });
	UTEST_NOT_NULL(TEXT("Listed compressed"), Listed);
	TestEqual(TEXT("Not comparing Kind, the block becomes one chunk"), Listed->GetNumPoints(), 1);

	// A different surface offset keeps a cell out too.
	UPCGPointArrayData* Offset = MakeVoxels(Block(2, 2, 2));
	Offset->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)->DeleteAttribute(TEXT("Id"));
	Offset->GetBoundsMaxValueRange()[3] = FVector(50, 50, 60);
	const UPCGBasePointData* OffsetOut = Compress(Offset);
	UTEST_NOT_NULL(TEXT("Offset compressed"), OffsetOut);
	TestEqual(TEXT("A cell with a different surface offset stays out of the chunk"), OffsetOut->GetNumPoints(), 8);

	// A listed attribute that does not exist is refused by name.
	AddExpectedError(TEXT("Listed Attributes names 'Nope'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("A missing listed attribute gives no output"), Compress(Voxels, EPCGVoxelChunkShape::Cubes, EPCGVoxelMatchAttributes::Listed, { TEXT("Nope") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelChunkRandomTest,
	"PCGUtils.Voxel.Chunks.RandomBoxesTileTheCellsExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelChunkRandomTest::RunTest(const FString&)
{
	using namespace PCGVoxelChunkTests;

	auto RandomCompress = [](const UPCGData* Voxels, const FIntVector& MaxSpan, int32 Seed) -> const UPCGBasePointData*
	{
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->Shape = EPCGVoxelChunkShape::Random;
		Settings->RandomSpan.Constant = FVector(MaxSpan);
		Settings->Seed = Seed;
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
	};
	auto SpansOf = [](const UPCGBasePointData* Points) -> TArray<FIntVector>
	{
		FPCGContext Context;
		FPCGVoxelGrid Grid;
		FPCGVoxelGrid::Build(&Context, Points, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Grid);
		return Grid.PointSpan;
	};

	// The 4x4x4 block: one odd corner cell, and colors split into a lower and an upper half.
	const UPCGPointArrayData* Plain = MakeBlock();
	const UPCGBasePointData* A = RandomCompress(Plain, FIntVector(3), 1);
	UTEST_NOT_NULL(TEXT("Random compressed"), A);
	TestTrue(TEXT("Random merges some cells"), A->GetNumPoints() < 64);

	const TArray<FIntVector> Spans = SpansOf(A);
	bool bWithinMax = true;
	bool bUneven = false;
	for (const FIntVector& Span : Spans)
	{
		bWithinMax &= Span.X >= 1 && Span.Y >= 1 && Span.Z >= 1 && Span.X <= 3 && Span.Y <= 3 && Span.Z <= 3;
		bUneven |= Span != Spans[0];
	}
	TestTrue(TEXT("Every box is within Random Max Span"), bWithinMax);
	TestTrue(TEXT("Boxes come in more than one size"), bUneven);

	// Expanding gives back every cell exactly once, each with its own Kind and color: boxes never mix unequal cells.
	const UPCGBasePointData* Expanded = Expand(A);
	UTEST_NOT_NULL(TEXT("Expanded"), Expanded);
	UTEST_EQUAL(TEXT("64 cells again"), Expanded->GetNumPoints(), 64);
	FPCGContext Context;
	FPCGVoxelGrid Before, After;
	UTEST_TRUE(TEXT("Original reads"), FPCGVoxelGrid::Build(&Context, Plain, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Before));
	UTEST_TRUE(TEXT("Round trip reads"), FPCGVoxelGrid::Build(&Context, Expanded, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, After));
	bool bSame = Before.Cells == After.Cells;
	for (const FIntVector& Cell : Before.Cells)
	{
		bSame &= KindAt(Plain, Before.FindPoint(Cell)) == KindAt(Expanded, After.FindPoint(Cell));
		bSame &= Plain->GetConstColorValueRange()[Before.FindPoint(Cell)] == Expanded->GetConstColorValueRange()[After.FindPoint(Cell)];
	}
	TestTrue(TEXT("Every cell is covered once and keeps its Kind and color"), bSame);

	// The seed decides the tiling.
	const UPCGBasePointData* Again = RandomCompress(Plain, FIntVector(3), 1);
	UTEST_NOT_NULL(TEXT("Again"), Again);
	TestTrue(TEXT("The same seed gives the same boxes"), SpansOf(Again) == Spans && Again->GetNumPoints() == A->GetNumPoints());
	bool bAnyDifferent = false;
	for (int32 Seed = 2; Seed < 6 && !bAnyDifferent; ++Seed)
	{
		const UPCGBasePointData* Other = RandomCompress(Plain, FIntVector(3), Seed);
		bAnyDifferent = Other && SpansOf(Other) != Spans;
	}
	TestTrue(TEXT("Another seed gives other boxes"), bAnyDifferent);

	// One cell thick along Z: a wall of flat tiles.
	const UPCGBasePointData* Flat = RandomCompress(Plain, FIntVector(4, 4, 1), 7);
	UTEST_NOT_NULL(TEXT("Flat"), Flat);
	bool bFlat = true;
	for (const FIntVector& Span : SpansOf(Flat))
	{
		bFlat &= Span.Z == 1;
	}
	TestTrue(TEXT("Random Max Span Z = 1 keeps every box one cell thick"), bFlat);

#if WITH_EDITOR
	// The palette alias presets the Random shape.
	UPCGVoxelCompressSettings* Preset = NewObject<UPCGVoxelCompressSettings>();
	const TArray<FPCGPreConfiguredSettingsInfo> Presets = Preset->GetPreconfiguredInfo();
	UTEST_EQUAL(TEXT("Two palette entries"), Presets.Num(), 2);
	Preset->ApplyPreconfiguredSettings(Presets[1]);
	TestEqual(TEXT("Voxel | Compress Random sets Shape Random"), Preset->Shape, EPCGVoxelChunkShape::Random);
	TestEqual(TEXT("Its title says Random"), Preset->GetDefaultNodeTitle().ToString(), FString(TEXT("Voxel | Compress Random")));
	Preset->ApplyPreconfiguredSettings(Presets[0]);
	TestEqual(TEXT("Voxel | Compress sets Shape Cubes"), Preset->Shape, EPCGVoxelChunkShape::Cubes);
#endif
	return true;
}

#endif
