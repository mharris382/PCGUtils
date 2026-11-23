// Copyright Max Harris
//
// The point selector layer on its own: the elements-as-points set (ranges, match rule, point data made only on
// demand) and Selector | In Bounds answering straight on point sets.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "PCGUtilsSelectorsTestHelpers.h"
#include "Points/PCGUtilsElementPoints.h"

namespace PCGUtilsSelectorsInBoundsTests
{
	using namespace PCGUtilsSelectorsTests;

	/** Four elements of one point each, at X 0, 100, 200, 300: boxes 100 across, or bare positions. */
	void FillRow(FPCGUtilsElementPoints& Points, bool bWithBounds)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Points.BeginElement(Index);
			const FVector Position(Index * 100.0, 0.0, 0.0);
			if (bWithBounds)
			{
				Points.AddPoint(FTransform(Position), FBox(FVector(-50.0), FVector(50.0)));
			}
			else
			{
				Points.AddPoint(Position);
			}
		}
	}

	/** The selector's answer over a point set, as text: one character per point. */
	FString Answer(const UPCGUtilsPointSelectorData* Selector, const FPCGUtilsElementPoints& Points)
	{
		const TSharedPtr<const FPCGUtilsPointSelectorOperation> Operation = Selector->CreateOperation(nullptr);
		TBitArray<> Mask;
		if (!Operation || !Operation->Select(Points, nullptr, Mask))
		{
			return TEXT("failed");
		}
		FString Text;
		for (int32 Index = 0; Index < Mask.Num(); ++Index)
		{
			Text += Mask[Index] ? TEXT("1") : TEXT("0");
		}
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsElementPointsTest,
	"PCGUtils.Selectors.ElementPoints", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsElementPointsTest::RunTest(const FString&)
{
	// Ranges: an element of one point, one of four, one of two.
	FPCGUtilsElementPoints Points;
	Points.BeginElement(7);
	Points.AddPoint(FVector(1.0, 0.0, 0.0));
	Points.BeginElement(9);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Points.AddPoint(FVector(10.0 + Index, 0.0, 0.0));
	}
	Points.BeginElement(11);
	Points.AddPoint(FTransform(FVector(20.0, 0.0, 0.0)), FBox(FVector(-1.0), FVector(1.0)));
	Points.AddPoint(FTransform(FVector(21.0, 0.0, 0.0)), FBox(ForceInit));

	TestEqual(TEXT("Three elements"), Points.NumElements(), 3);
	TestEqual(TEXT("Seven points"), Points.NumPoints(), 7);
	TestEqual(TEXT("Element ids are the domain's"), Points.GetElementId(1), 9);
	TestEqual(TEXT("The second element starts at point 1"), Points.GetFirstPoint(1), 1);
	TestEqual(TEXT("The second element has four points"), Points.GetNumPointsOf(1), 4);
	TestEqual(TEXT("The third element starts at point 5"), Points.GetFirstPoint(2), 5);
	TestTrue(TEXT("A point with a box is noticed"), Points.HasAnyBounds());
	TestTrue(TEXT("An invalid box is stored as no bounds"), Points.GetLocalBounds()[6].IsValid && Points.GetLocalBounds()[6].GetSize().IsZero());

	// The match rule: two of the four points of the middle element selected.
	TBitArray<> PointMask(false, 7);
	PointMask[1] = true;
	PointMask[2] = true;
	PointMask[5] = true;
	PointMask[6] = true;
	TBitArray<> Elements;
	Points.Match = EPCGUtilsElementPointMatch::Any;
	Points.Reduce(PointMask, Elements);
	TestTrue(TEXT("Any: one point missing, two of four, both of two"), !Elements[0] && Elements[1] && Elements[2]);
	Points.Match = EPCGUtilsElementPointMatch::All;
	Points.Reduce(PointMask, Elements);
	TestTrue(TEXT("All: two of four is not all"), !Elements[0] && !Elements[1] && Elements[2]);
	Points.Match = EPCGUtilsElementPointMatch::AtLeastFraction;
	Points.MatchFraction = 0.5;
	Points.Reduce(PointMask, Elements);
	TestTrue(TEXT("At least half: two of four is enough"), Elements[1]);
	Points.MatchFraction = 0.75;
	Points.Reduce(PointMask, Elements);
	TestFalse(TEXT("At least three quarters: two of four is not"), Elements[1]);
	TestFalse(TEXT("A single-point element ignores the rule: its one point decides"), Elements[0]);

	// Point data: none until asked, then made once.
	const int32 TotalBefore = FPCGUtilsElementPoints::GetTotalPointDataBuildCount();
	TestEqual(TEXT("No point data yet"), Points.GetPointDataBuildCount(), 0);
	const UPCGBasePointData* First = Points.GetOrBuildPointData(nullptr);
	const UPCGBasePointData* Second = Points.GetOrBuildPointData(nullptr);
	if (TestNotNull(TEXT("Point data is made on demand"), First))
	{
		TestTrue(TEXT("The second call returns the same data"), First == Second);
		TestEqual(TEXT("Made once"), Points.GetPointDataBuildCount(), 1);
		TestEqual(TEXT("Counted once"), FPCGUtilsElementPoints::GetTotalPointDataBuildCount(), TotalBefore + 1);
		TestEqual(TEXT("One PCG point per point"), First->GetNumPoints(), 7);
		TestEqual(TEXT("Transforms carried"), First->GetConstTransformValueRange()[4].GetLocation(), FVector(13.0, 0.0, 0.0));
		TestEqual(TEXT("Bounds carried"), First->GetConstBoundsMaxValueRange()[5], FVector(1.0));
	}

	// A domain may supply its own builder.
	FPCGUtilsElementPoints Custom;
	Custom.BeginElement(0);
	Custom.AddPoint(FVector::ZeroVector);
	bool bCustomCalled = false;
	Custom.SetPointDataBuilder([&bCustomCalled](const FPCGUtilsElementPoints& Set, FPCGContext* Context)
	{
		bCustomCalled = true;
		return FPCGUtilsElementPoints::BuildDefaultPointData(Set, Context);
	});
	TestNotNull(TEXT("The custom builder's data is returned"), Custom.GetOrBuildPointData(nullptr));
	TestTrue(TEXT("The custom builder was used"), bCustomCalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorInBoundsPointsTest,
	"PCGUtils.Selectors.InBounds.Points", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorInBoundsPointsTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsInBoundsTests;
	const int32 TotalBefore = FPCGUtilsElementPoints::GetTotalPointDataBuildCount();

	FPCGUtilsElementPoints Boxes;
	FillRow(Boxes, true);
	FPCGUtilsElementPoints Bare;
	FillRow(Bare, false);

	// A bound from X -60 to 160.
	const FVector Center(50.0, 0.0, 0.0);
	const FVector Wide(110.0, 60.0, 60.0);

	// Points with bounds: the three containment modes differ.
	TestEqual(TEXT("Center: the positions at 0 and 100"), Answer(MakeInBounds(Center, Wide, EPCGUtilsSelectorContainment::Center), Boxes), FString(TEXT("1100")));
	TestEqual(TEXT("Any Part: the box from 150 to 250 is touched too"), Answer(MakeInBounds(Center, Wide, EPCGUtilsSelectorContainment::AnyPart), Boxes), FString(TEXT("1110")));
	TestEqual(TEXT("Fully Inside: a bound from -60 to 140 holds only the first box whole"),
		Answer(MakeInBounds(FVector(40.0, 0.0, 0.0), FVector(100.0, 60.0, 60.0), EPCGUtilsSelectorContainment::FullyInside), Boxes), FString(TEXT("1000")));

	// Points with no bounds: all three are the same test.
	for (const EPCGUtilsSelectorContainment Containment : { EPCGUtilsSelectorContainment::Center, EPCGUtilsSelectorContainment::AnyPart, EPCGUtilsSelectorContainment::FullyInside })
	{
		TestEqual(TEXT("No bounds: every containment mode gives the positions at 0 and 100"), Answer(MakeInBounds(Center, Wide, Containment), Bare), FString(TEXT("1100")));
	}

	// Expand, in world units.
	TestEqual(TEXT("Expand 50: the bound reaches 210 and takes the position at 200"), Answer(MakeInBounds(Center, Wide, EPCGUtilsSelectorContainment::Center, 50.0), Bare), FString(TEXT("1110")));
	TestEqual(TEXT("Expand -70: the bound shrinks to 10 to 90 and holds no position"), Answer(MakeInBounds(Center, Wide, EPCGUtilsSelectorContainment::Center, -70.0), Bare), FString(TEXT("0000")));
	AddExpectedMessage(TEXT("bounds are inside out"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("Expand -200: the bound is inside out, skipped with a warning, and nothing is selected"), Answer(MakeInBounds(Center, Wide, EPCGUtilsSelectorContainment::Center, -200.0), Bare), FString(TEXT("0000")));

	// A turned bound: a thin box along the XY diagonal.
	{
		UPCGUtilsInBoundsPointSelectorData* Diagonal = NewObject<UPCGUtilsInBoundsPointSelectorData>();
		Diagonal->PointData.Add(MakeBoxPoint(FVector::ZeroVector, FVector(100.0, 10.0, 10.0), FRotator(0.0, 45.0, 0.0)));
		Diagonal->Containment = EPCGUtilsSelectorContainment::AnyPart;

		FPCGUtilsElementPoints Points;
		Points.BeginElement(0);
		Points.AddPoint(FVector(60.0, 60.0, 0.0));
		Points.BeginElement(1);
		Points.AddPoint(FVector(60.0, -60.0, 0.0));
		Points.BeginElement(2);
		Points.AddPoint(FTransform(FVector(60.0, -60.0, 0.0)), FBox(FVector(-70.0), FVector(70.0)));
		Points.BeginElement(3);
		Points.AddPoint(FTransform(FVector(60.0, -60.0, 0.0)), FBox(FVector(-20.0), FVector(20.0)));
		TestEqual(TEXT("Turned bound: on the diagonal, off it, a box reaching it, a box that does not"), Answer(Diagonal, Points), FString(TEXT("1010")));
		TestEqual(TEXT("In Bounds read transforms and bounds only"), Points.GetPointDataBuildCount(), 0);
	}

	// Nothing on the Bounds pin selects nothing.
	TestEqual(TEXT("No bounds to test against: nothing selected"), Answer(NewObject<UPCGUtilsInBoundsPointSelectorData>(), Bare), FString(TEXT("0000")));

	TestEqual(TEXT("In Bounds never asked a set with bounds for point data"), Boxes.GetPointDataBuildCount(), 0);
	TestEqual(TEXT("In Bounds never asked a bare set for point data"), Bare.GetPointDataBuildCount(), 0);
	TestEqual(TEXT("No point data was made anywhere"), FPCGUtilsElementPoints::GetTotalPointDataBuildCount(), TotalBefore);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
