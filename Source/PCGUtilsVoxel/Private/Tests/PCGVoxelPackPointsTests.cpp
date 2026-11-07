// Copyright Max Harris
//
// Voxel | Pack Points: each cell claimed once, Max Radius respected, metadata kept, and part-way boxes never overlap.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelPackPoints.h"
#include "Math/RandomStream.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelPackPointsTests
{
	using namespace PCGVoxelTestHelpers;

	/** Loose points at the given locations (and rotations), each with an 'Id' equal to its index. */
	UPCGPointArrayData* MakeLoose(const TArray<FVector>& Locations, const TArray<FQuat>* Rotations = nullptr)
	{
		TArray<FIntVector> Dummy;
		Dummy.SetNum(Locations.Num());
		for (int32 I = 0; I < Dummy.Num(); ++I)
		{
			Dummy[I] = FIntVector(I * 10, 0, 0);
		}
		UPCGPointArrayData* Points = MakeVoxels(Dummy);
		auto Transforms = Points->GetTransformValueRange();
		for (int32 I = 0; I < Locations.Num(); ++I)
		{
			Transforms[I] = FTransform(Rotations ? (*Rotations)[I] : FQuat::Identity, Locations[I], FVector(0.3));
		}
		return Points;
	}

	TArray<FPCGTaggedData> Pack(UPCGVoxelPackPointsSettings* Settings, const UPCGData* Loose, const UPCGData* Grid)
	{
		return Run<FPCGVoxelPackPointsElement>(Settings, {
			{ PCGPinConstants::DefaultInputLabel, Loose }, { PCGVoxelPackPoints::GridLabel, Grid } });
	}

	const UPCGBasePointData* Output(const TArray<FPCGTaggedData>& Outputs, FName Pin)
	{
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (Tagged.Pin == Pin)
			{
				return Cast<UPCGBasePointData>(Tagged.Data);
			}
		}
		return nullptr;
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Default)
	{
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Default;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPackClaimsTest,
	"PCGUtils.Voxel.PackPoints.EachCellIsClaimedOnceWithinMaxRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelPackClaimsTest::RunTest(const FString&)
{
	using namespace PCGVoxelPackPointsTests;

	const UPCGPointArrayData* Grid = MakeVoxels(Block(3, 1, 1));
	const UPCGPointArrayData* Loose = MakeLoose({ FVector(0, 0, 300), FVector(10, 0, 300), FVector(100, 0, 250), FVector(200, 0, 300), FVector(5000, 0, 0) });
	UPCGVoxelPackPointsSettings* Settings = NewObject<UPCGVoxelPackPointsSettings>();

	const TArray<FPCGTaggedData> Outputs = Pack(Settings, Loose, Grid);
	const UPCGBasePointData* Out = Output(Outputs, PCGPinConstants::DefaultOutputLabel);
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	UTEST_EQUAL(TEXT("Every point comes out (unpacked ones are kept)"), Out->GetNumPoints(), 5);

	const int32 ExpectedTarget[5] = { 0, 1, 2, -1, -1 };
	for (int32 I = 0; I < 5; ++I)
	{
		TestEqual(*FString::Printf(TEXT("Point %d keeps its Id"), I), ValueOf<int32>(Out, TEXT("Id"), I, -9), I);
		TestEqual(*FString::Printf(TEXT("Point %d claims grid point %d"), I, ExpectedTarget[I]), ValueOf<int32>(Out, TEXT("VoxelTargetIndex"), I, -9), ExpectedTarget[I]);
		TestEqual(*FString::Printf(TEXT("Point %d Packed"), I), ValueOf<bool>(Out, TEXT("Packed"), I, false), ExpectedTarget[I] >= 0);
	}
	const auto Transforms = Out->GetConstTransformValueRange();
	TestTrue(TEXT("Point 1 sits at cell (1, 0, 0)"), Transforms[1].GetLocation().Equals(FVector(100, 0, 0), 0.01));
	TestTrue(TEXT("and has scale 1"), Transforms[1].GetScale3D().Equals(FVector::OneVector));
	TestTrue(TEXT("and cell-sized bounds"), Out->GetConstBoundsMaxValueRange()[1].Equals(FVector(50.0)));
	TestTrue(TEXT("An unpacked point stays where it was"), Transforms[4].GetLocation().Equals(FVector(5000, 0, 0), 0.01));
	TestTrue(TEXT("Tags carry over"), Outputs[0].Tags.Contains(TEXT("KeptTag")));

	const UPCGBasePointData* Unfilled = Output(Outputs, PCGVoxelPackPoints::UnfilledLabel);
	UTEST_NOT_NULL(TEXT("Unfilled Cells exists"), Unfilled);
	TestEqual(TEXT("Nothing is left unfilled"), Unfilled->GetNumPoints(), 0);

	// With a 100 radius nobody is close enough.
	Settings->MaxRadius = 100.0;
	const TArray<FPCGTaggedData> Near = Pack(Settings, Loose, Grid);
	const UPCGBasePointData* NearOut = Output(Near, PCGPinConstants::DefaultOutputLabel);
	UTEST_NOT_NULL(TEXT("Out exists (radius 100)"), NearOut);
	TestFalse(TEXT("A point 300 above its nearest cell is not packed within 100"), ValueOf<bool>(NearOut, TEXT("Packed"), 0, true));
	TestEqual(TEXT("All three cells are left unfilled"), Output(Near, PCGVoxelPackPoints::UnfilledLabel)->GetNumPoints(), 3);

	Settings->UnpackedPoints = EPCGVoxelUnpackedPoints::Remove;
	TestEqual(TEXT("Remove drops the unpacked points"), Output(Pack(Settings, Loose, Grid), PCGPinConstants::DefaultOutputLabel)->GetNumPoints(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPackOverlapTest,
	"PCGUtils.Voxel.PackPoints.PartlySettledBoxesNeverOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelPackOverlapTest::RunTest(const FString&)
{
	using namespace PCGVoxelPackPointsTests;

	FRandomStream Random(42);
	TArray<FVector> Locations;
	TArray<FQuat> Rotations;
	for (int32 I = 0; I < 40; ++I)
	{
		Locations.Add(FVector(Random.FRandRange(-50, 550), Random.FRandRange(-50, 550), Random.FRandRange(150, 450)));
		Rotations.Add(FRotator(Random.FRandRange(-180, 180), Random.FRandRange(-180, 180), Random.FRandRange(-180, 180)).Quaternion());
	}
	const UPCGPointArrayData* Grid = MakeVoxels(Block(6, 6, 1));
	const UPCGPointArrayData* Loose = MakeLoose(Locations, &Rotations);

	UPCGVoxelPackPointsSettings* Settings = NewObject<UPCGVoxelPackPointsSettings>();
	Settings->PositionSettle.Source = EPCGVoxelSettleSource::Random;
	Settings->RotationSettle.Source = EPCGVoxelSettleSource::Random;
	const UPCGBasePointData* Out = Output(Pack(Settings, Loose, Grid), PCGPinConstants::DefaultOutputLabel);
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	TestTrue(*FString::Printf(TEXT("Most points survive (%d of 40)"), Out->GetNumPoints()), Out->GetNumPoints() >= 20);

	const auto Transforms = Out->GetConstTransformValueRange();
	int32 Overlaps = 0;
	int32 PartWay = 0;
	for (int32 A = 0; A < Out->GetNumPoints(); ++A)
	{
		const float Settle = ValueOf<float>(Out, TEXT("Settle"), A, 1.0f);
		PartWay += (Settle > 0.0f && Settle < 1.0f) ? 1 : 0;
		for (int32 B = A + 1; B < Out->GetNumPoints(); ++B)
		{
			Overlaps += PCGVoxelPackPoints::BoxesOverlap(Transforms[A].GetLocation(), Transforms[A].GetRotation(), FVector(49.9),
				Transforms[B].GetLocation(), Transforms[B].GetRotation(), FVector(49.9)) ? 1 : 0;
		}
	}
	TestEqual(TEXT("No two output boxes intersect"), Overlaps, 0);
	TestTrue(*FString::Printf(TEXT("Some boxes are left part way (%d)"), PartWay), PartWay > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelPackPieceTest,
	"PCGUtils.Voxel.PackPoints.APointWithASpanNeedsAWholeFreeBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelPackPieceTest::RunTest(const FString&)
{
	using namespace PCGVoxelPackPointsTests;

	// A 2x1x1 brick over the left end of a 3-cell row takes cells 0 and 1 and leaves cell 2.
	const UPCGPointArrayData* Grid = MakeVoxels(Block(3, 1, 1));
	const TArray<FIntVector> Spans = { FIntVector(2, 1, 1) };
	UPCGPointArrayData* Brick = MakeVoxels({ FIntVector(0, 0, 3) }, 100.0, &Spans);
	UPCGVoxelPackPointsSettings* Settings = NewObject<UPCGVoxelPackPointsSettings>();
	const TArray<FPCGTaggedData> Outputs = Pack(Settings, Brick, Grid);
	const UPCGBasePointData* Out = Output(Outputs, PCGPinConstants::DefaultOutputLabel);
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	TestTrue(TEXT("The brick sits between cells 0 and 1"), Out->GetConstTransformValueRange()[0].GetLocation().Equals(FVector(50, 0, 0), 0.01));
	TestTrue(TEXT("and its bounds cover both"), Out->GetConstBoundsMaxValueRange()[0].Equals(FVector(100, 50, 50)));
	TestEqual(TEXT("One cell is left"), Output(Outputs, PCGVoxelPackPoints::UnfilledLabel)->GetNumPoints(), 1);

	// The closest rotation keeps the box's shape: a cube may turn a quarter, a brick may not swap its long axis.
	const FQuat Turned = FQuat(FVector::UpVector, FMath::DegreesToRadians(80.0));
	TestTrue(TEXT("A cube turned 80 degrees settles at 90"),
		PCGVoxelPackPoints::ClosestBoxRotation(Turned, FVector(50)).AngularDistance(FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI)) < 1e-4);
	TestTrue(TEXT("A brick turned 80 degrees settles back at 0"),
		PCGVoxelPackPoints::ClosestBoxRotation(Turned, FVector(100, 50, 50)).AngularDistance(FQuat::Identity) < 1e-4);
	return true;
}

#endif
