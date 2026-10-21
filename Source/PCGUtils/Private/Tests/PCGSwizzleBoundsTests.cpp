#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Elements/PCGSwizzleBounds.h"
#include "PCGContext.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace SwizzleBoundsTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

class FExecute : public FPCGSwizzleBoundsElement
{
public:
	using FPCGSwizzleBoundsElement::ExecuteInternal;
};

TArray<FVector> WorldCorners(const FTransform& Transform, const FVector& Min, const FVector& Max)
{
	TArray<FVector> Corners;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? Max.X : Min.X, (Corner & 2) ? Max.Y : Min.Y, (Corner & 4) ? Max.Z : Min.Z);
		Corners.Add(Transform.TransformPosition(Local));
	}
	return Corners;
}

bool SameCornerSet(const TArray<FVector>& A, const TArray<FVector>& B)
{
	for (const FVector& Corner : A)
	{
		if (!B.ContainsByPredicate([&Corner](const FVector& Other) { return Corner.Equals(Other, 1e-3); })) return false;
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwizzleBoundsGeometryTest, "PCGUtils.Points.SwizzleBounds.BoxUnchanged", SwizzleBoundsTests::Flags)
bool FSwizzleBoundsGeometryTest::RunTest(const FString&)
{
	using namespace SwizzleBoundsTests;
	// Rotated, non-uniformly scaled point with off-center bounds, so every term of the remap is exercised.
	const FTransform Source(FRotator(17.0, 40.0, -25.0), FVector(100.0, -50.0, 30.0), FVector(1.5, 0.5, 2.0));
	const FVector SourceMin(-10.0, -3.0, -1.0), SourceMax(30.0, 5.0, 2.0);
	const TArray<FVector> Expected = WorldCorners(Source, SourceMin, SourceMax);

	const EPCGUtilsSwizzleAxes AllAxes[] = {EPCGUtilsSwizzleAxes::XY, EPCGUtilsSwizzleAxes::XZ, EPCGUtilsSwizzleAxes::YZ};
	const int32 Pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		for (int32 Flip = 0; Flip < 2; ++Flip)
		{
			for (int32 PivotIndex = 0; PivotIndex < 2; ++PivotIndex)
			{
				const EPCGUtilsSwizzlePivot Pivot = PivotIndex ? EPCGUtilsSwizzlePivot::LocalCenter : EPCGUtilsSwizzlePivot::PointLocation;
				const FString Case = FString::Printf(TEXT("axes %d flip %d pivot %d"), AxisIndex, Flip, PivotIndex);
				FTransform T = Source; FVector Min = SourceMin, Max = SourceMax;
				PCGUtilsSwizzleBounds::Apply(AllAxes[AxisIndex], Flip != 0, Pivot, T, Min, Max);

				TestTrue(FString::Printf(TEXT("World box unchanged (%s)"), *Case), SameCornerSet(Expected, WorldCorners(T, Min, Max)));
				TestTrue(FString::Printf(TEXT("Rotation stays a proper rotation (%s)"),
					*Case), FMath::IsNearlyEqual(T.GetRotation().Size(), 1.0, 1e-6) && T.GetScale3D().GetMin() > 0.0);

				const int32 A = Pairs[AxisIndex][0], B = Pairs[AxisIndex][1];
				const FVector NewAxisA = T.GetRotation().GetAxisX() * (A == 0) + T.GetRotation().GetAxisY() * (A == 1) + T.GetRotation().GetAxisZ() * (A == 2);
				const FVector OldAxisB = Source.GetRotation().GetAxisX() * (B == 0) + Source.GetRotation().GetAxisY() * (B == 1) + Source.GetRotation().GetAxisZ() * (B == 2);
				TestTrue(FString::Printf(TEXT("New first axis lies along old second axis (%s)"), *Case),
					FMath::IsNearlyEqual(FMath::Abs(NewAxisA | OldAxisB), 1.0, 1e-6));
				TestTrue(FString::Printf(TEXT("Flip decides the direction (%s)"), *Case), Flip ? (NewAxisA | OldAxisB) < 0.0 : (NewAxisA | OldAxisB) > 0.0);
				TestEqual(FString::Printf(TEXT("Extents swap (%s)"), *Case), (Max - Min)[A], (SourceMax - SourceMin)[B]);

				if (Pivot == EPCGUtilsSwizzlePivot::PointLocation)
				{
					TestTrue(FString::Printf(TEXT("Point location kept (%s)"), *Case), T.GetLocation().Equals(Source.GetLocation(), 1e-6));
				}
				else
				{
					TestTrue(FString::Printf(TEXT("Local center kept (%s)"), *Case), ((Min + Max) * 0.5).Equals((SourceMin + SourceMax) * 0.5, 1e-6));
				}
			}
		}
	}

	// Four quarter turns about the same axis return the original point.
	FTransform T = Source; FVector Min = SourceMin, Max = SourceMax;
	for (int32 Step = 0; Step < 4; ++Step) PCGUtilsSwizzleBounds::Apply(EPCGUtilsSwizzleAxes::XZ, false, EPCGUtilsSwizzlePivot::PointLocation, T, Min, Max);
	TestTrue(TEXT("Four swizzles are identity"), T.Equals(Source, 1e-6) && Min.Equals(SourceMin, 1e-6) && Max.Equals(SourceMax, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwizzleBoundsNodeTest, "PCGUtils.Points.SwizzleBounds.PreservesInput", SwizzleBoundsTests::Flags)
bool FSwizzleBoundsNodeTest::RunTest(const FString&)
{
	auto* Settings = NewObject<UPCGSwizzleBoundsSettings>();
	auto* Source = NewObject<UPCGPointArrayData>();
	Source->SetNumPoints(3);
	auto* Elements = Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	auto* Id = Elements->CreateAttribute<int32>(TEXT("TestId"), -1, false);
	auto* Data = Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
	Data->CreateAttribute<int32>(TEXT("TestData"), 7, false);
	auto Transforms = Source->GetTransformValueRange();
	auto Keys = Source->GetMetadataEntryValueRange();
	auto Density = Source->GetDensityValueRange();
	auto Min = Source->GetBoundsMinValueRange();
	auto Max = Source->GetBoundsMaxValueRange();
	for (int32 I = 0; I < 3; ++I)
	{
		Transforms[I] = FTransform(FRotator(0, 30 * I, 0), FVector(I * 100, 0, 0), FVector(1, 2, 3));
		Keys[I] = Elements->AddEntry(); Id->SetValue(Keys[I], 10 + I);
		Density[I] = 0.25f * (I + 1);
		Min[I] = FVector(-40, -5, -1); Max[I] = FVector(40, 5, 1);
	}

	FPCGContext Context;
	Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
	auto& Input = Context.InputData.TaggedData.Emplace_GetRef();
	Input.Data = Source; Input.Pin = PCGPinConstants::DefaultInputLabel; Input.Tags.Add(TEXT("PreservedTag"));

	SwizzleBoundsTests::FExecute Execute;
	TestTrue(TEXT("Node executes"), Execute.ExecuteInternal(&Context));
	if (!TestEqual(TEXT("One output per input"), Context.OutputData.TaggedData.Num(), 1)) return false;

	const FPCGTaggedData& Output = Context.OutputData.TaggedData[0];
	const auto* Out = Cast<UPCGBasePointData>(Output.Data);
	if (!TestNotNull(TEXT("Output is point data"), Out)) return false;
	TestTrue(TEXT("Output is a new object"), Out != Source);
	TestTrue(TEXT("Tags kept"), Output.Tags.Contains(TEXT("PreservedTag")));
	TestEqual(TEXT("Point count kept"), Out->GetNumPoints(), 3);
	TestNotNull(TEXT("@Data attribute kept"), Out->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data)->GetConstAttribute(TEXT("TestData")));
	const auto* OutId = Out->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("TestId"));
	if (!TestNotNull(TEXT("Element attribute kept"), OutId)) return false;
	auto OutKeys = Out->GetConstMetadataEntryValueRange();
	auto OutDensity = Out->GetConstDensityValueRange();
	auto OutMin = Out->GetConstBoundsMinValueRange();
	auto OutMax = Out->GetConstBoundsMaxValueRange();
	for (int32 I = 0; I < 3; ++I)
	{
		TestEqual(TEXT("Attribute values follow their points"), OutId->GetValueFromItemKey(OutKeys[I]), 10 + I);
		TestEqual(TEXT("Density kept"), OutDensity[I], 0.25f * (I + 1));
		TestTrue(TEXT("Long side moved from X to Y"), (OutMax[I] - OutMin[I]).Equals(FVector(10, 80, 2)));
	}
	return true;
}
#endif
