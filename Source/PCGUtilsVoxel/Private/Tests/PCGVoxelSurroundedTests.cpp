// Copyright Max Harris
//
// The voxel contract's 'is surrounded' attribute stays true to the cells: nodes that change cells rewrite it.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelBoolean.h"
#include "Elements/PCGVoxelCompress.h"
#include "Elements/PCGVoxelExpand.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelSurroundedTests
{
	using namespace PCGVoxelTestHelpers;

	/** Writes a Boolean 'IsSurrounded' per point from Value(cell). */
	void AddSurrounded(UPCGPointArrayData* Points, const TArray<FIntVector>& Cells, TFunctionRef<bool(const FIntVector&)> Value)
	{
		FPCGMetadataDomain* Domain = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		Domain->DeleteAttribute(TEXT("Id")); // unique per point, it would keep Compress from merging anything
		FPCGMetadataAttribute<bool>* Attribute = Domain->CreateAttribute<bool>(TEXT("IsSurrounded"), false, false);
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Attribute->SetValue(Entries[Index], Value(Cells[Index]));
		}
	}

	/** True inside a 6x6x6 block at the origin (cells 1 to 4 on every axis). */
	bool InsideSix(const FIntVector& C)
	{
		return C.X >= 1 && C.X <= 4 && C.Y >= 1 && C.Y <= 4 && C.Z >= 1 && C.Z <= 4;
	}

	/** Points of the single output, how many there are and how many have IsSurrounded true (-1 when missing). */
	TPair<int32, int32> Count(const TArray<FPCGTaggedData>& Outputs)
	{
		const UPCGBasePointData* Points = Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
		if (!Points)
		{
			return { -1, -1 };
		}
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const auto* Attribute = Domain ? static_cast<const FPCGMetadataAttribute<bool>*>(Domain->GetConstAttribute(TEXT("IsSurrounded"))) : nullptr;
		if (!Attribute)
		{
			return { Points->GetNumPoints(), -1 };
		}
		int32 True = 0;
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			True += Attribute->GetValueFromItemKey(Entries[Index]) ? 1 : 0;
		}
		return { Points->GetNumPoints(), True };
	}

	TArray<FPCGTaggedData> Compress(const UPCGData* In, int32 MaxSpan, FName SurroundedName = TEXT("IsSurrounded"))
	{
		UPCGVoxelCompressSettings* Settings = NewObject<UPCGVoxelCompressSettings>();
		Settings->Shape = EPCGVoxelChunkShape::Cubes;
		Settings->MaxSpan.Constant = MaxSpan;
		Settings->Grid.SurroundedAttribute = SurroundedName;
		return Run<FPCGVoxelCompressElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelSurroundedTest, "PCGUtils.Voxel.Surrounded.KeptByNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelSurroundedTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelSurroundedTests;
	const TArray<FIntVector> Six = Block(6, 6, 6);

	// Compress ignores the attribute when comparing cells, so inside and surface cells merge alike: 27 cubes of 2.
	// Only the middle cube (2..3 on every axis) shows no face.
	UPCGPointArrayData* Correct = MakeVoxels(Six);
	AddSurrounded(Correct, Six, InsideSix);
	const TArray<FPCGTaggedData> Compressed = Compress(Correct, 2);
	TestEqual(TEXT("compress: 27 cubes (inside and surface cells merge)"), Count(Compressed).Key, 27);
	TestEqual(TEXT("compress: only the middle cube is surrounded"), Count(Compressed).Value, 1);

	// A stale value (all false) is corrected the same way.
	UPCGPointArrayData* Stale = MakeVoxels(Six);
	AddSurrounded(Stale, Six, [](const FIntVector&) { return false; });
	TestEqual(TEXT("stale input: the middle cube is surrounded"), Count(Compress(Stale, 2)).Value, 1);

	// One 4-cube over a 4x4x4 block shows faces.
	UPCGPointArrayData* Four = MakeVoxels(Block(4, 4, 4));
	AddSurrounded(Four, Block(4, 4, 4), [](const FIntVector& C) { return C.X >= 1 && C.X <= 2 && C.Y >= 1 && C.Y <= 2 && C.Z >= 1 && C.Z <= 2; });
	const TPair<int32, int32> FourCount = Count(Compress(Four, 4));
	TestTrue(TEXT("4x4x4: one chunk, not surrounded"), FourCount.Key == 1 && FourCount.Value == 0);

	// Expand back to cells: the inner 4x4x4 is surrounded again.
	if (Compressed.Num() == 1)
	{
		UPCGVoxelExpandSettings* Expand = NewObject<UPCGVoxelExpandSettings>();
		const TPair<int32, int32> Expanded = Count(Run<FPCGVoxelExpandElement>(Expand, { { PCGPinConstants::DefaultInputLabel, Compressed[0].Data } }));
		TestEqual(TEXT("expand: 216 cells"), Expanded.Key, 216);
		TestEqual(TEXT("expand: the inner 64 are surrounded"), Expanded.Value, 64);
	}

	// Subtract the cell (3,3,3): it goes, and its six face neighbours now show a face. 64 - 1 - 6.
	{
		UPCGPointArrayData* In = MakeVoxels(Six);
		AddSurrounded(In, Six, InsideSix);
		UPCGVoxelBooleanSettings* Boolean = NewObject<UPCGVoxelBooleanSettings>();
		Boolean->Operation = EPCGVoxelBooleanOperation::Subtract;
		const TPair<int32, int32> Cut = Count(Run<FPCGVoxelBooleanElement>(Boolean,
			{ { PCGPinConstants::DefaultInputLabel, In }, { PCGVoxelBooleanPins::Operand, MakeVoxels({ FIntVector(3) }) } }));
		TestEqual(TEXT("subtract: 215 cells"), Cut.Key, 215);
		TestEqual(TEXT("subtract: 57 surrounded"), Cut.Value, 57);
	}

	// Surrounded Attribute None: the attribute is an ordinary value again (compared, never rewritten).
	UPCGPointArrayData* Untouched = MakeVoxels(Six);
	AddSurrounded(Untouched, Six, [](const FIntVector&) { return false; });
	TestEqual(TEXT("None: values left alone"), Count(Compress(Untouched, 2, NAME_None)).Value, 0);
	return true;
}

#endif
