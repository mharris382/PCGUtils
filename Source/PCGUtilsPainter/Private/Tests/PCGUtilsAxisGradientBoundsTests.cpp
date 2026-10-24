// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Math/RandomStream.h"

namespace PCGUtilsAxisGradientBoundsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using PCGUtilsAxisGradientBounds::ESpanCase;

	/** Brute force: the smallest perpendicular distance from the line through Origin to a dense sampling of the box. */
	double BruteForceNearestLineDistance(const FBox& Box, const FVector& Origin, const FVector& Axis)
	{
		const int32 N = 80;
		double Best = UE_DOUBLE_BIG_NUMBER;
		for (int32 X = 0; X <= N; ++X) for (int32 Y = 0; Y <= N; ++Y) for (int32 Z = 0; Z <= N; ++Z)
		{
			const FVector D = Box.Min + (Box.Max - Box.Min) * FVector(X, Y, Z) / N - Origin;
			Best = FMath::Min(Best, (D - Axis * (D | Axis)).Size());
		}
		return Best;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsAxisGradientSpanTest, "PCGUtils.DynMesh.Painter.AxisGradient.Span", PCGUtilsAxisGradientBoundsTests::Flags)
bool FPCGUtilsAxisGradientSpanTest::RunTest(const FString&)
{
	using namespace PCGUtilsAxisGradientBoundsTests;
	const FBox Box(FVector(-50, -50, 0), FVector(50, 50, 80));
	ESpanCase Case;

	auto Check = [&](const TCHAR* What, const FVector& Origin, const FVector& Axis, double Expected, ESpanCase ExpectedCase)
	{
		const double Span = PCGUtilsAxisGradientBounds::Measure(Box, Origin, Axis.GetSafeNormal(), &Case);
		TestEqual(FString::Printf(TEXT("%s: span"), What), Span, Expected, 1e-6);
		TestEqual(FString::Printf(TEXT("%s: case"), What), static_cast<int32>(Case), static_cast<int32>(ExpectedCase));
	};

	Check(TEXT("Bottom center, up"), FVector(0, 0, 0), FVector::UpVector, 80.0, ESpanCase::Direct);
	Check(TEXT("Middle, up"), FVector(0, 0, 40), FVector::UpVector, 40.0, ESpanCase::Direct);
	Check(TEXT("Below the box, up: exits the top"), FVector(0, 0, -20), FVector::UpVector, 100.0, ESpanCase::Direct);
	Check(TEXT("Top, down"), FVector(10, 10, 80), -FVector::UpVector, 80.0, ESpanCase::Direct);
	Check(TEXT("Beside the box, up: nearest parallel line runs up its side"), FVector(200, 0, 0), FVector::UpVector, 80.0, ESpanCase::Shifted);
	Check(TEXT("Beside the box partway up"), FVector(200, 300, 30), FVector::UpVector, 50.0, ESpanCase::Shifted);
	Check(TEXT("Beside and below"), FVector(-200, 0, -40), FVector::UpVector, 120.0, ESpanCase::Shifted);
	Check(TEXT("Above, pointing away: box behind, thickness used"), FVector(0, 0, 200), FVector::UpVector, 80.0, ESpanCase::Behind);
	Check(TEXT("Corner, diagonal"), FVector(-50, -50, 10), FVector(1, 1, 0), 100.0 * UE_DOUBLE_SQRT_2, ESpanCase::Direct);
	Check(TEXT("Line misses along X: nearest is the y=50 face"), FVector(0, 200, 10), FVector(1, 0, 0), 50.0, ESpanCase::Shifted);
	// Tilted axis from beside the box: slides to x=50, then (0,0.6,0.8) leaves through z=80 at t=100 before y=50 at t=83.3.
	Check(TEXT("Tilted axis from beside"), FVector(300, 0, 0), FVector(0, 0.6, 0.8), 50.0 / 0.6, ESpanCase::Shifted);

	// Random origins and axes. Checks: the line used is the nearest parallel one (against brute force), the exit point
	// lies on the box, and a step further along the axis is outside it.
	FRandomStream Random(1234);
	int32 Shifted = 0;
	const FBox Grown = Box.ExpandBy(1e-3);
	for (int32 Trial = 0; Trial < 60; ++Trial)
	{
		const FVector Origin = FVector(Random.FRandRange(-250, 250), Random.FRandRange(-250, 250), Random.FRandRange(-150, 250));
		const FVector Axis = Random.GetUnitVector();
		FVector LineOrigin;
		const double Span = PCGUtilsAxisGradientBounds::Measure(Box, Origin, Axis, &Case, &LineOrigin);
		const FString Label = FString::Printf(TEXT("Random %d (origin %s, axis %s)"), Trial, *Origin.ToString(), *Axis.ToString());
		const FVector Moved = LineOrigin - Origin;
		TestTrue(Label + TEXT(": origin only moves sideways"), FMath::Abs(Moved | Axis) < 1e-6);
		TestTrue(Label + TEXT(": moved onto the nearest parallel line"),
			FMath::Abs(Moved.Size() - BruteForceNearestLineDistance(Box, Origin, Axis)) < 1.5);
		if (Case == ESpanCase::Behind)
		{
			continue;
		}
		Shifted += Case == ESpanCase::Shifted;
		const FVector Exit = LineOrigin + Axis * Span;
		TestTrue(Label + TEXT(": exit point is on the box"), Grown.IsInsideOrOn(Exit));
		TestFalse(Label + TEXT(": past the exit is outside"), Grown.IsInsideOrOn(Exit + Axis * 0.1));
	}
	TestTrue(TEXT("Random trials covered origins whose line misses the box"), Shifted > 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsAxisGradientBoundsPainterTest, "PCGUtils.DynMesh.Painter.AxisGradient.BoundsRelative", PCGUtilsAxisGradientBoundsTests::Flags)
bool FPCGUtilsAxisGradientBoundsPainterTest::RunTest(const FString&)
{
	// A table-like box: 200 x 100 footprint, 20 to 100 high, off the local origin.
	UE::Geometry::FDynamicMesh3 Mesh;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		Mesh.AppendVertex(FVector3d(Corner & 1 ? 300.0 : 100.0, Corner & 2 ? 50.0 : -50.0, Corner & 4 ? 100.0 : 20.0));
	}

	UPCGDynMeshAxisGradientPainterFactoryData* Gradient = NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
	const UPCGDynMeshAxisGradientPainterProviderSettings* Defaults = GetDefault<UPCGDynMeshAxisGradientPainterProviderSettings>();
	Gradient->OriginMode = EPCGUtilsAxisGradientOriginMode::BoundsRelative;
	Gradient->OriginAlignment = Defaults->OriginAlignment; // Center X/Y, bottom Z.
	Gradient->Axis = FVector::UpVector;
	Gradient->bDistancesRelativeToBounds = true;
	Gradient->StartDistance = 0.0f;
	Gradient->EndDistance = 1.0f;

	auto Evaluate = [&](const FTransform& LocalToWorld, const FVector& Local)
	{
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, LocalToWorld);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Gradient->CreateOperation(nullptr);
		if (!Operation || !Operation->Initialize(Context) || !Operation->Prepare(Context))
		{
			AddError(TEXT("Axis Gradient did not initialize and prepare"));
			return -1.0f;
		}
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.LocalPosition = Local;
		Sample.WorldPosition = LocalToWorld.TransformPosition(Local);
		return Operation->Evaluate(Sample).Scalar;
	};

	TestEqual(TEXT("Bottom of the box is 0"), Evaluate(FTransform::Identity, FVector(200, 0, 20)), 0.0f, 1e-5f);
	TestEqual(TEXT("Middle of the box is 0.5"), Evaluate(FTransform::Identity, FVector(120, 30, 60)), 0.5f, 1e-5f);
	TestEqual(TEXT("Top of the box is 1"), Evaluate(FTransform::Identity, FVector(300, -50, 100)), 1.0f, 1e-5f);

	// Origin as an offset: 10 up from the bottom, so 0 is at z=30 and 1 is the remaining 70 to the top.
	Gradient->Origin = FVector(0, 0, 10);
	TestEqual(TEXT("Offset origin: z=65 is half way"), Evaluate(FTransform::Identity, FVector(200, 0, 65)), 0.5f, 1e-5f);
	Gradient->Origin = FVector::ZeroVector;

	// World space uses the world box of the transformed mesh: scaled 2x and lifted 1000, the box spans z 1040 to 1200.
	Gradient->CoordinateSpace = EPCGUtilsDynMeshPainterCoordinateSpace::World;
	const FTransform LocalToWorld(FRotator(0, 37, 0), FVector(0, 0, 1000), FVector(2.0));
	TestEqual(TEXT("World space: local z=60 is half way"), Evaluate(LocalToWorld, FVector(150, 20, 60)), 0.5f, 1e-4f);

	// Absolute distances with a bounds-relative origin: 0 at the bottom, 1 after 40 units.
	Gradient->CoordinateSpace = EPCGUtilsDynMeshPainterCoordinateSpace::Local;
	Gradient->bDistancesRelativeToBounds = false;
	Gradient->EndDistance = 40.0f;
	TestEqual(TEXT("Absolute range from the aligned origin"), Evaluate(FTransform::Identity, FVector(200, 0, 40)), 0.5f, 1e-5f);
	return true;
}

#endif
