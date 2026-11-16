// Copyright Max Harris
//
// Voxel | Fill Gaps: thin gaps (closing) and enclosed cavities filled; added cells copy a filled neighbour; chunks kept.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelFillGaps.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelFillGapsTests
{
	using namespace PCGVoxelTestHelpers;

	struct FResult
	{
		const UPCGBasePointData* Out = nullptr;
		const UPCGBasePointData* Gaps = nullptr;
	};

	FResult Fill(const UPCGData* In, TFunctionRef<void(UPCGVoxelFillGapsSettings&)> Edit)
	{
		UPCGVoxelFillGapsSettings* Settings = NewObject<UPCGVoxelFillGapsSettings>();
		Edit(*Settings);
		FResult Result;
		for (const FPCGTaggedData& Data : Run<FPCGVoxelFillGapsElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } }))
		{
			(Data.Pin == PCGVoxelFillGapsPins::Gaps ? Result.Gaps : Result.Out) = Cast<UPCGBasePointData>(Data.Data);
		}
		return Result;
	}

	int32 Num(const UPCGBasePointData* Points) { return Points ? Points->GetNumPoints() : -1; }

	int32 PointAt(const UPCGBasePointData* Points, const FIntVector& Cell)
	{
		const auto Transforms = Points->GetConstTransformValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			if (Transforms[Index].GetLocation().Equals(FVector(Cell) * 100.0, 0.01))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Default)
	{
		const FPCGMetadataDomain* Domain = Points && Index != INDEX_NONE ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Default;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}

	int32 CountTrue(const UPCGBasePointData* Points, FName Name)
	{
		int32 Count = 0;
		for (int32 Index = 0; Points && Index < Points->GetNumPoints(); ++Index)
		{
			Count += ValueOf<bool>(Points, Name, Index, false) ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelFillGapsTest, "PCGUtils.Voxel.FillGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelFillGapsTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelFillGapsTests;
	const int32 XY = static_cast<int32>(EPCGVoxelAxes::X | EPCGVoxelAxes::Y);

	// A one-cell slot in a plate (x = 2, y = 1 to 4), looked at in plan: the 4 slot cells come back.
	TArray<FIntVector> Slotted = Block(5, 5, 1);
	Slotted.RemoveAll([](const FIntVector& C) { return C.X == 2 && C.Y >= 1; });
	const FResult Slot = Fill(MakeVoxels(Slotted), [&](UPCGVoxelFillGapsSettings& S) { S.Axes = XY; });
	TestEqual(TEXT("slot: Out is the whole plate"), Num(Slot.Out), 25);
	TestEqual(TEXT("slot: Gaps has the 4 slot cells"), Num(Slot.Gaps), 4);
	TestEqual(TEXT("slot: IsGap on the 4 added cells"), CountTrue(Slot.Out, TEXT("IsGap")), 4);
	if (Slot.Out)
	{
		// Added cells copy a face neighbour: (2, 1) is next to (2, 0), (1, 1) and (3, 1).
		const int32 Copied = ValueOf<int32>(Slot.Out, TEXT("Id"), PointAt(Slot.Out, FIntVector(2, 1, 0)), -9);
		const int32 Below = Slotted.IndexOfByKey(FIntVector(2, 0, 0));
		const int32 Left = Slotted.IndexOfByKey(FIntVector(1, 1, 0));
		const int32 Right = Slotted.IndexOfByKey(FIntVector(3, 1, 0));
		TestTrue(TEXT("slot: an added cell copies a face neighbour"), Copied == Below || Copied == Left || Copied == Right);
		TestTrue(TEXT("slot: added cells have whole-cell bounds"), Slot.Out->GetConstBoundsMinValueRange()[PointAt(Slot.Out, FIntVector(2, 3, 0))].Equals(FVector(-50.0), 1e-6));
	}

	// A slot three wide is not a gap at radius 1, but is at radius 2.
	TArray<FIntVector> Wide = Block(7, 5, 1);
	Wide.RemoveAll([](const FIntVector& C) { return C.X >= 2 && C.X <= 4 && C.Y >= 1; });
	TestEqual(TEXT("wide slot, radius 1: nothing added"), Num(Fill(MakeVoxels(Wide), [&](UPCGVoxelFillGapsSettings& S) { S.Axes = XY; }).Gaps), 0);
	TestEqual(TEXT("wide slot, radius 2: filled"), Num(Fill(MakeVoxels(Wide), [&](UPCGVoxelFillGapsSettings& S) { S.Axes = XY; S.GapRadius = 2; }).Gaps), 12);

	// A 5x5x5 shell with a 3x3x3 hollow: too big for radius 1 gaps, but Fill Cavities fills all 27, flagged.
	TArray<FIntVector> Shell = Block(5, 5, 5);
	Shell.RemoveAll([](const FIntVector& C) { return C.X >= 1 && C.X <= 3 && C.Y >= 1 && C.Y <= 3 && C.Z >= 1 && C.Z <= 3; });
	TestEqual(TEXT("shell: gaps alone add nothing"), Num(Fill(MakeVoxels(Shell), [](UPCGVoxelFillGapsSettings&) {}).Gaps), 0);
	const FResult Sealed = Fill(MakeVoxels(Shell), [](UPCGVoxelFillGapsSettings& S) { S.bFillCavities = true; S.bWriteIsCavity = true; });
	TestEqual(TEXT("shell: cavities filled, a solid 5-cube"), Num(Sealed.Out), 125);
	TestEqual(TEXT("shell: IsCavity on the 27 added"), CountTrue(Sealed.Gaps, TEXT("IsCavity")), 27);
	TestTrue(TEXT("shell: the middle copies a filled cell"), ValueOf<int32>(Sealed.Out, TEXT("Id"), PointAt(Sealed.Out, FIntVector(2, 2, 2)), -9) >= 0);

	// Open the top of the hollow: a cup is not a cavity.
	TArray<FIntVector> Cup = Shell;
	Cup.RemoveAll([](const FIntVector& C) { return C.X >= 1 && C.X <= 3 && C.Y >= 1 && C.Y <= 3 && C.Z == 4; });
	TestEqual(TEXT("cup: not a cavity"), Num(Fill(MakeVoxels(Cup), [](UPCGVoxelFillGapsSettings& S) { S.bFillGaps = false; S.bFillCavities = true; }).Gaps), 0);

	// A small hollow is both: Fill Gaps fills it and IsCavity marks it.
	TArray<FIntVector> Hollow = Block(3, 3, 3);
	Hollow.Remove(FIntVector(1, 1, 1));
	const FResult Small = Fill(MakeVoxels(Hollow), [](UPCGVoxelFillGapsSettings& S) { S.bWriteIsCavity = true; });
	TestEqual(TEXT("small hollow: filled as a gap"), Num(Small.Out), 27);
	TestEqual(TEXT("small hollow: flagged as a cavity"), CountTrue(Small.Out, TEXT("IsCavity")), 1);

	// Chunks are kept: a 2x1x1 chunk and a cell with a one-cell gap between them.
	const TArray<FIntVector> Spans = { FIntVector(2, 1, 1), FIntVector(1, 1, 1) };
	const FResult Chunked = Fill(MakeVoxels({ FIntVector(0, 0, 0), FIntVector(3, 0, 0) }, 100.0, &Spans), [](UPCGVoxelFillGapsSettings&) {});
	TestEqual(TEXT("chunked: the chunk, the cell and the gap"), Num(Chunked.Out), 3);
	TestEqual(TEXT("chunked: one gap"), Num(Chunked.Gaps), 1);

	// Nothing to fill: Out is the input as it was.
	TestEqual(TEXT("both off: passes through"), Num(Fill(MakeVoxels(Slotted), [](UPCGVoxelFillGapsSettings& S) { S.bFillGaps = false; }).Out), Slotted.Num());

	// Names: each enabled output needs its own.
	AddExpectedErrorPlain(TEXT("Is Gap Attribute and Is Cavity Attribute are both"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("same names: no output"), Fill(MakeVoxels(Slotted), [](UPCGVoxelFillGapsSettings& S)
	{
		S.bWriteIsCavity = true;
		S.IsCavityAttribute = S.IsGapAttribute;
	}).Out);
	return true;
}

#endif
