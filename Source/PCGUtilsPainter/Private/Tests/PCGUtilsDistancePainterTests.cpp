// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshDistancePainter.h"
#include "Elements/Painters/PCGUtilsPainterDistanceField.h"
#include "Math/RandomStream.h"

namespace PCGUtilsDistancePainterTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using PCGUtilsPainterDistance::FDistanceField;

	UPCGDynMeshDistancePainterFactoryData* MakeFactory(TArray<FPCGUtilsPainterDistancePoint> Points)
	{
		UPCGDynMeshDistancePainterFactoryData* Factory = NewObject<UPCGDynMeshDistancePainterFactoryData>();
		Factory->Points = MoveTemp(Points);
		return Factory;
	}

	FPCGUtilsPainterDistancePoint MakePoint(const FVector& Location, const FVector& Extents = FVector::OneVector,
		const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Scale = FVector::OneVector)
	{
		FPCGUtilsPainterDistancePoint Point;
		Point.Transform = FTransform(Rotation, Location, Scale);
		Point.BoundsMin = -Extents;
		Point.BoundsMax = Extents;
		return Point;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterDefaultsTest, "PCGUtils.DynMesh.Painter.Distance.Defaults", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterDefaultsTest::RunTest(const FString&)
{
	const UPCGDynMeshDistancePainterProviderSettings* Settings = GetDefault<UPCGDynMeshDistancePainterProviderSettings>();
	TestEqual(TEXT("Palette title"), Settings->GetDefaultNodeTitle().ToString(), FString(TEXT("Painter | Distance")));
	TestEqual(TEXT("Min Radius defaults to 0"), Settings->MinRadius, 0.0);
	TestEqual(TEXT("Max Radius defaults to 200"), Settings->MaxRadius, 200.0);
	TestEqual(TEXT("In range blends from 1"), Settings->ValueAtMinRadius, 1.0f);
	TestEqual(TEXT("In range blends to 0"), Settings->ValueAtMaxRadius, 0.0f);
	TestEqual(TEXT("Out of range defaults to 0"), Settings->OutOfRangeValue, 0.0f);
	TestEqual(TEXT("Default curve is the identity at 0.25"), Settings->FalloffCurve.GetRichCurveConst()->Eval(0.25f), 0.25f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterRangeTest, "PCGUtils.DynMesh.Painter.Distance.Range", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterRangeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDistancePainterTests;
	UPCGDynMeshDistancePainterFactoryData* Factory = MakeFactory({MakePoint(FVector(1000, 0, 0))});
	Factory->OutOfRangeValue = 0.25f;
	Factory->bClampValue = false;
	auto At = [&](double X) { return FDistanceField(*Factory).Evaluate(FVector(1000 + X, 0, 0)); };

	TestEqual(TEXT("On the point is Value At Min Radius"), At(0), 1.0f, 1e-5f);
	TestEqual(TEXT("Half way is half way"), At(100), 0.5f, 1e-5f);
	TestEqual(TEXT("At Max Radius is Value At Max Radius"), At(200), 0.0f, 1e-5f);
	TestEqual(TEXT("Past Max Radius is Out Of Range Value"), At(250), 0.25f, 1e-5f);

	Factory->MinRadius = 50.0;
	TestEqual(TEXT("Solid core inside Min Radius"), At(20), 1.0f, 1e-5f);
	TestEqual(TEXT("Falloff starts at Min Radius"), At(125), 0.5f, 1e-5f);
	Factory->InsideMinRadius = EPCGUtilsPainterDistanceInsideMin::OutOfRange;
	TestEqual(TEXT("Inside Min Radius can be out of range"), At(20), 0.25f, 1e-5f);
	TestEqual(TEXT("Exactly at Min Radius is in range"), At(50), 1.0f, 1e-5f);

	Factory->MinRadius = 0.0;
	Factory->InsideMinRadius = EPCGUtilsPainterDistanceInsideMin::SolidCore;
	Factory->ValueAtMinRadius = 0.0f;
	Factory->ValueAtMaxRadius = 1.0f;
	TestEqual(TEXT("0 to 1 makes a ring"), At(150), 0.75f, 1e-5f);
	Factory->ValueAtMinRadius = 1.0f;
	TestEqual(TEXT("1 to 1 makes a hard dot"), At(199), 1.0f, 1e-5f);

	Factory->MinRadius = 100.0;
	Factory->MaxRadius = 100.0;
	TestEqual(TEXT("Equal radii are a step: inside"), At(99), 1.0f, 1e-5f);
	TestEqual(TEXT("Equal radii are a step: outside"), At(101), 0.25f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterShapesTest, "PCGUtils.DynMesh.Painter.Distance.ShapesAndTypes", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterShapesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDistancePainterTests;
	UPCGDynMeshDistancePainterFactoryData* Factory = MakeFactory({MakePoint(FVector::ZeroVector, FVector(10))});
	const FVector Off(60, 80, 0);

	TestEqual(TEXT("Euclidean"), FDistanceField(*Factory).DistanceTo(0, Off), 100.0, 1e-9);
	Factory->Type = EPCGUtilsPainterDistanceMetric::Manhattan;
	TestEqual(TEXT("Manhattan"), FDistanceField(*Factory).DistanceTo(0, Off), 140.0, 1e-9);
	Factory->Type = EPCGUtilsPainterDistanceMetric::Chebyshev;
	TestEqual(TEXT("Chebyshev"), FDistanceField(*Factory).DistanceTo(0, Off), 80.0, 1e-9);
	Factory->Type = EPCGUtilsPainterDistanceMetric::Euclidean;

	// Sphere: radius is the length of the scaled extents, sqrt(300) here.
	Factory->Target = EPCGUtilsPainterDistanceShape::SphereBounds;
	const double SphereRadius = FMath::Sqrt(300.0);
	TestEqual(TEXT("Sphere: outside"), FDistanceField(*Factory).DistanceTo(0, FVector(100, 0, 0)), 100.0 - SphereRadius, 1e-9);
	TestEqual(TEXT("Sphere: inside is 0 with Overlap Is Zero"), FDistanceField(*Factory).DistanceTo(0, FVector(5, 0, 0)), 0.0, 1e-9);
	Factory->bOverlapIsZero = false;
	TestEqual(TEXT("Sphere: inside measures to the surface without it"), FDistanceField(*Factory).DistanceTo(0, FVector(5, 0, 0)), SphereRadius - 5.0, 1e-9);

	// Box: oriented and scaled. Local extents 10 x 20 x 5, scaled 2x, yawed 90 degrees: local +X points to world +Y.
	Factory->Points = {MakePoint(FVector(500, 0, 0), FVector(10, 20, 5), FRotator(0, 90, 0), FVector(2))};
	Factory->Target = EPCGUtilsPainterDistanceShape::BoxBounds;
	TestEqual(TEXT("Box: inside is 0"), FDistanceField(*Factory).DistanceTo(0, FVector(500, 15, 3)), 0.0, 1e-6);
	TestEqual(TEXT("Box: off the rotated +X face"), FDistanceField(*Factory).DistanceTo(0, FVector(500, 50, 0)), 30.0, 1e-6);
	TestEqual(TEXT("Box: off the rotated Y face"), FDistanceField(*Factory).DistanceTo(0, FVector(400, 0, 0)), 60.0, 1e-6);
	TestEqual(TEXT("Box: inside paints the full value"), FDistanceField(*Factory).Evaluate(FVector(500, 15, 3)), 1.0f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterBlendTest, "PCGUtils.DynMesh.Painter.Distance.BlendAndFalloff", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterBlendTest::RunTest(const FString&)
{
	using namespace PCGUtilsDistancePainterTests;
	// A vertex at x=40 is 40 from the first point (value 0.8) and 60 from the second (value 0.7).
	UPCGDynMeshDistancePainterFactoryData* Factory = MakeFactory({MakePoint(FVector(0, 0, 0)), MakePoint(FVector(100, 0, 0))});
	Factory->bClampValue = false;
	const FVector Vertex(40, 0, 0);
	auto Check = [&](EPCGUtilsPainterDistanceBlend Blend, float Expected, const TCHAR* What)
	{
		Factory->Blend = Blend;
		TestEqual(What, FDistanceField(*Factory).Evaluate(Vertex), Expected, 1e-5f);
	};
	Check(EPCGUtilsPainterDistanceBlend::Nearest, 0.8f, TEXT("Nearest"));
	Check(EPCGUtilsPainterDistanceBlend::Max, 0.8f, TEXT("Max"));
	Check(EPCGUtilsPainterDistanceBlend::Min, 0.7f, TEXT("Min"));
	Check(EPCGUtilsPainterDistanceBlend::Average, 0.75f, TEXT("Average"));
	Check(EPCGUtilsPainterDistanceBlend::Add, 1.5f, TEXT("Add"));
	Check(EPCGUtilsPainterDistanceBlend::Multiply, 0.56f, TEXT("Multiply"));
	Factory->bClampValue = true;
	Check(EPCGUtilsPainterDistanceBlend::Add, 1.0f, TEXT("Add, clamped"));

	// Nearest takes the nearest point's value even when another point's is larger.
	Factory->Blend = EPCGUtilsPainterDistanceBlend::Nearest;
	Factory->ValueAtMinRadius = 0.0f;
	Factory->ValueAtMaxRadius = 1.0f;
	TestEqual(TEXT("Nearest with a ring takes the nearer point"), FDistanceField(*Factory).Evaluate(Vertex), 0.2f, 1e-5f);
	Factory->ValueAtMinRadius = 1.0f;
	Factory->ValueAtMaxRadius = 0.0f;

	// Falloffs at t = 0.5 (distance 100 of 200) from a single point.
	Factory->Points = {MakePoint(FVector::ZeroVector)};
	const FVector Half(100, 0, 0);
	Factory->Falloff = EPCGUtilsPainterDistanceFalloff::Smooth;
	TestEqual(TEXT("Smooth at half"), FDistanceField(*Factory).Evaluate(Half), 0.5f, 1e-5f);
	Factory->Falloff = EPCGUtilsPainterDistanceFalloff::Power;
	Factory->Exponent = 2.0f;
	TestEqual(TEXT("Power 2 at half"), FDistanceField(*Factory).Evaluate(Half), 0.75f, 1e-5f);
	Factory->Falloff = EPCGUtilsPainterDistanceFalloff::Exponential;
	Factory->Steepness = 4.0f;
	TestEqual(TEXT("Exponential 4 at half"), FDistanceField(*Factory).Evaluate(Half),
		static_cast<float>(1.0 - (1.0 - FMath::Exp(-2.0)) / (1.0 - FMath::Exp(-4.0))), 1e-5f);
	TestEqual(TEXT("Exponential reaches the end value at Max Radius"), FDistanceField(*Factory).Evaluate(FVector(200, 0, 0)), 0.0f, 1e-5f);
	Factory->Falloff = EPCGUtilsPainterDistanceFalloff::Curve;
	Factory->Curve.Reset();
	Factory->Curve.AddKey(0.0f, 0.0f);
	Factory->Curve.AddKey(0.5f, 1.0f);
	TestEqual(TEXT("Curve: blend 0.5 at t 0.25"), FDistanceField(*Factory).Evaluate(FVector(50, 0, 0)), 0.5f, 1e-5f);
	TestEqual(TEXT("Curve: blend 1 from t 0.5"), FDistanceField(*Factory).Evaluate(Half), 0.0f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterGridTest, "PCGUtils.DynMesh.Painter.Distance.GridMatchesBruteForce", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterGridTest::RunTest(const FString&)
{
	using namespace PCGUtilsDistancePainterTests;
	FRandomStream Random(777);
	TArray<FPCGUtilsPainterDistancePoint> Points;
	for (int32 Index = 0; Index < 300; ++Index)
	{
		// A few very large points so some influences cover too many cells and go on the every-vertex list.
		const bool bHuge = Index % 50 == 0;
		Points.Add(MakePoint(
			FVector(Random.FRandRange(-2000, 2000), Random.FRandRange(-2000, 2000), Random.FRandRange(-300, 300)),
			bHuge ? FVector(20000) : FVector(Random.FRandRange(1, 60), Random.FRandRange(1, 60), Random.FRandRange(1, 60)),
			FRotator(Random.FRandRange(-180, 180), Random.FRandRange(-180, 180), Random.FRandRange(-180, 180)),
			FVector(Random.FRandRange(0.5, 2.0), Random.FRandRange(0.5, 2.0), Random.FRandRange(0.5, 2.0))));
	}
	UPCGDynMeshDistancePainterFactoryData* Factory = MakeFactory(MoveTemp(Points));
	Factory->MinRadius = 20.0;
	Factory->MaxRadius = 150.0;
	Factory->bClampValue = false;

	TArray<FVector> Samples;
	for (int32 Index = 0; Index < 1500; ++Index)
	{
		Samples.Add(FVector(Random.FRandRange(-2300, 2300), Random.FRandRange(-2300, 2300), Random.FRandRange(-500, 500)));
	}

	const EPCGUtilsPainterDistanceBlend Blends[] = {EPCGUtilsPainterDistanceBlend::Nearest, EPCGUtilsPainterDistanceBlend::Add, EPCGUtilsPainterDistanceBlend::Min};
	int32 Mismatches = 0;
	int32 InRange = 0;
	for (int32 Shape = 0; Shape < 3; ++Shape)
	{
		for (int32 Metric = 0; Metric < 3; ++Metric)
		{
			for (const EPCGUtilsPainterDistanceBlend Blend : Blends)
			{
				Factory->Target = static_cast<EPCGUtilsPainterDistanceShape>(Shape);
				Factory->Type = static_cast<EPCGUtilsPainterDistanceMetric>(Metric);
				Factory->Blend = Blend;
				Factory->InsideMinRadius = Metric == 1 ? EPCGUtilsPainterDistanceInsideMin::OutOfRange : EPCGUtilsPainterDistanceInsideMin::SolidCore;
				const FDistanceField Field(*Factory);
				if (Shape == 2)
				{
					TestTrue(TEXT("Large boxes go on the every-vertex list"), Field.NumGlobalPoints() > 0);
				}
				for (const FVector& Sample : Samples)
				{
					const float Grid = Field.Evaluate(Sample);
					const float Brute = Field.EvaluateBruteForce(Sample);
					InRange += Brute != Factory->OutOfRangeValue;
					if (!FMath::IsNearlyEqual(Grid, Brute, 1e-4f))
					{
						if (++Mismatches <= 5)
						{
							AddError(FString::Printf(TEXT("Shape %d metric %d blend %d at %s: grid %f, brute force %f"),
								Shape, Metric, static_cast<int32>(Blend), *Sample.ToString(), Grid, Brute));
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("Grid lookup matches brute force everywhere"), Mismatches, 0);
	TestTrue(TEXT("Many samples were in range of some point"), InRange > 1000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDistancePainterOperationTest, "PCGUtils.DynMesh.Painter.Distance.Operation", PCGUtilsDistancePainterTests::Flags)
bool FPCGUtilsDistancePainterOperationTest::RunTest(const FString&)
{
	using namespace PCGUtilsDistancePainterTests;
	UE::Geometry::FDynamicMesh3 Mesh;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Mesh.AppendVertex(FVector3d(Index * 50.0, 0, 0));
	}
	// The mesh sits at x=1000 in the world; the point is at the world position of local x=0.
	const FTransform LocalToWorld(FVector(1000, 0, 0));
	UPCGDynMeshDistancePainterFactoryData* Factory = MakeFactory({MakePoint(FVector(1000, 0, 0))});

	const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, LocalToWorld);
	const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Factory->CreateOperation(nullptr);
	if (!Operation || !Operation->Initialize(Context) || !Operation->Prepare(Context))
	{
		AddError(TEXT("Distance Painter did not initialize and prepare"));
		return false;
	}
	for (int32 VertexID = 0; VertexID < 5; ++VertexID)
	{
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.VertexID = VertexID;
		Sample.LocalPosition = FVector(Mesh.GetVertex(VertexID));
		Sample.WorldPosition = LocalToWorld.TransformPosition(Sample.LocalPosition);
		TestEqual(FString::Printf(TEXT("Prepared vertex %d"), VertexID), Operation->Evaluate(Sample).Scalar,
			1.0f - VertexID * 50.0f / 200.0f, 1e-5f);
	}
	FPCGUtilsDynMeshPainterSample Loose;
	Loose.WorldPosition = FVector(1100, 0, 0);
	TestEqual(TEXT("A sample that is not a vertex is evaluated on the spot"), Operation->Evaluate(Loose).Scalar, 0.5f, 1e-5f);
	return true;
}

#endif
