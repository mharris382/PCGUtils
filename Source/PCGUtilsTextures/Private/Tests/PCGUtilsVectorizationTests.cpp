// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/ImagePathComponent.h"
#include "Engine/Texture2D.h"
#include "Image/PCGUtilsGreyscaleImage.h"
#include "Vectorization/PCGUtilsVectorization.h"
#include "Vectorization/PCGUtilsVectorizationLibrary.h"

namespace PCGUtilsVectorizationTests
{
	/** A filled ring: 1 where Inner < distance from Center < Outer, 0 elsewhere. */
	FPCGUtilsGreyscaleImage MakeRing(int32 Size, FVector2D Center, double Inner, double Outer)
	{
		FPCGUtilsGreyscaleImage Image;
		Image.Init(Size, Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double Distance = FVector2D::Distance(FVector2D(X + 0.5, Y + 0.5), Center);
				Image.At(X, Y) = Distance > Inner && Distance < Outer ? 1.0f : 0.0f;
			}
		}
		return Image;
	}

	FPCGUtilsGreyscaleImage MakeSquare(int32 Size, int32 Min, int32 Max)
	{
		FPCGUtilsGreyscaleImage Image;
		Image.Init(Size, Size);
		for (int32 Y = Min; Y < Max; ++Y)
		{
			for (int32 X = Min; X < Max; ++X)
			{
				Image.At(X, Y) = 1.0f;
			}
		}
		return Image;
	}

	/** The path as drawn: a curve path's Hermite segments sampled densely, a polyline as it is. */
	TArray<FVector2D> DrawnPoints(const FPCGUtilsVectorPath& Path)
	{
		if (!Path.HasTangents())
		{
			return Path.Points;
		}
		TArray<FVector2D> Dense;
		const int32 Num = Path.Points.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const int32 Next = (Index + 1) % Num;
			for (int32 Step = 0; Step < 16; ++Step)
			{
				Dense.Add(FMath::CubicInterp(Path.Points[Index], Path.LeaveTangents[Index], Path.Points[Next], Path.ArriveTangents[Next], Step / 16.0f));
			}
		}
		return Dense;
	}

	/** Checks a ring traced into exactly an outer boundary and its hole, with sane areas, nesting and winding. */
	void CheckRing(FAutomationTestBase& Test, const TCHAR* Name, const TArray<FPCGUtilsVectorPath>& Paths, double Inner, double Outer, double Tolerance)
	{
		if (!Test.TestEqual(*FString::Printf(TEXT("%s: a ring is two paths"), Name), Paths.Num(), 2))
		{
			return;
		}
		const FPCGUtilsVectorPath& Boundary = Paths[0];
		const FPCGUtilsVectorPath& Hole = Paths[1];
		Test.TestFalse(*FString::Printf(TEXT("%s: the first path is the outer boundary"), Name), Boundary.bIsHole);
		Test.TestEqual(*FString::Printf(TEXT("%s: the outer boundary has no parent"), Name), Boundary.ParentIndex, INDEX_NONE);
		Test.TestTrue(*FString::Printf(TEXT("%s: the second path is the hole"), Name), Hole.bIsHole);
		Test.TestEqual(*FString::Printf(TEXT("%s: the hole's parent is the outer boundary"), Name), Hole.ParentIndex, 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: the hole is one level deep"), Name), Hole.Depth, 1);

		const double OuterArea = UE_DOUBLE_PI * Outer * Outer;
		const double InnerArea = UE_DOUBLE_PI * Inner * Inner;
		Test.TestTrue(*FString::Printf(TEXT("%s: outer area %.1f is near %.1f"), Name, Boundary.Area, OuterArea),
			FMath::Abs(Boundary.Area - OuterArea) < OuterArea * Tolerance);
		Test.TestTrue(*FString::Printf(TEXT("%s: hole area %.1f is near %.1f"), Name, Hole.Area, InnerArea),
			FMath::Abs(Hole.Area - InnerArea) < InnerArea * Tolerance * 2.0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the outer boundary winds positive"), Name), PCGUtilsVectorization::SignedArea(DrawnPoints(Boundary)) > 0.0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the hole winds negative"), Name), PCGUtilsVectorization::SignedArea(DrawnPoints(Hole)) < 0.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsVectorizationRingTest,
	"PCGUtils.Textures.Vectorize.EveryAlgorithmTracesARing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsVectorizationRingTest::RunTest(const FString&)
{
	using namespace PCGUtilsVectorizationTests;
	const FPCGUtilsGreyscaleImage Ring = MakeRing(64, FVector2D(32, 32), 10.0, 20.0);

	TArray<FPCGUtilsVectorPath> Paths;
	PCGUtilsVectorization::MarchingSquares(Ring, FPCGUtilsMarchingSquaresOptions(), Paths);
	CheckRing(*this, TEXT("Marching Squares"), Paths, 10.0, 20.0, 0.05);

	PCGUtilsVectorization::BorderFollowing(Ring, FPCGUtilsBorderFollowingOptions(), Paths);
	// Border following runs through boundary pixel centers, half a pixel inside the true edge.
	CheckRing(*this, TEXT("Border Following"), Paths, 10.5, 19.5, 0.06);

	PCGUtilsVectorization::CurveFit(Ring, FPCGUtilsCurveFitOptions(), Paths);
	CheckRing(*this, TEXT("Curve Fit"), Paths, 10.0, 20.0, 0.05);
	if (Paths.Num() == 2)
	{
		TestTrue(TEXT("Curve Fit: paths carry tangents"), Paths[0].HasTangents() && Paths[1].HasTangents());
		TestTrue(TEXT("Curve Fit: a smooth circle needs few curve points"), Paths[0].Points.Num() <= 16);
	}

	// Outer Only drops the hole.
	FPCGUtilsMarchingSquaresOptions OuterOnly;
	OuterOnly.Retrieval = EPCGUtilsContourRetrieval::OuterOnly;
	PCGUtilsVectorization::MarchingSquares(Ring, OuterOnly, Paths);
	TestEqual(TEXT("Outer Only keeps just the outer boundary"), Paths.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsVectorizationDetailTest,
	"PCGUtils.Textures.Vectorize.CornersSpecklesAndThinRings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsVectorizationDetailTest::RunTest(const FString&)
{
	using namespace PCGUtilsVectorizationTests;

	// Curve Fit keeps a square's corners as path points instead of rounding them off.
	{
		const FPCGUtilsGreyscaleImage Square = MakeSquare(64, 20, 44);
		TArray<FPCGUtilsVectorPath> Paths;
		PCGUtilsVectorization::CurveFit(Square, FPCGUtilsCurveFitOptions(), Paths);
		if (TestEqual(TEXT("A square is one path"), Paths.Num(), 1))
		{
			for (const FVector2D Corner : {FVector2D(20, 20), FVector2D(44, 20), FVector2D(44, 44), FVector2D(20, 44)})
			{
				double Nearest = TNumericLimits<double>::Max();
				for (const FVector2D& Point : Paths[0].Points)
				{
					Nearest = FMath::Min(Nearest, FVector2D::Distance(Point, Corner));
				}
				TestTrue(*FString::Printf(TEXT("Curve Fit keeps the corner at %s (nearest point %.2f px)"), *Corner.ToString(), Nearest), Nearest < 1.5);
			}
		}
	}

	// A speckle below Min Area is dropped; the same speckle is kept with Min Area 0.
	{
		FPCGUtilsGreyscaleImage Image = MakeSquare(32, 8, 24);
		Image.At(2, 2) = 1.0f;
		Image.At(3, 2) = 1.0f;
		TArray<FPCGUtilsVectorPath> Paths;
		PCGUtilsVectorization::MarchingSquares(Image, FPCGUtilsMarchingSquaresOptions(), Paths);
		TestEqual(TEXT("A two-pixel speckle is dropped at Min Area 4"), Paths.Num(), 1);
		FPCGUtilsMarchingSquaresOptions KeepAll;
		KeepAll.MinArea = 0.0f;
		PCGUtilsVectorization::MarchingSquares(Image, KeepAll, Paths);
		TestEqual(TEXT("The speckle is kept at Min Area 0"), Paths.Num(), 2);
	}

	// A one-pixel-wide square outline: both borders run through the same pixels, so only Suzuki-Abe's own
	// hierarchy can tell the hole from the outside. It must still report outer + hole.
	{
		FPCGUtilsGreyscaleImage Outline;
		Outline.Init(32, 32);
		for (int32 Index = 8; Index <= 24; ++Index)
		{
			Outline.At(Index, 8) = Outline.At(Index, 24) = Outline.At(8, Index) = Outline.At(24, Index) = 1.0f;
		}
		FPCGUtilsBorderFollowingOptions Options;
		Options.Approximation = EPCGUtilsContourApproximation::None;
		TArray<FPCGUtilsVectorPath> Paths;
		PCGUtilsVectorization::BorderFollowing(Outline, Options, Paths);
		if (TestEqual(TEXT("A one-pixel outline is two borders"), Paths.Num(), 2))
		{
			TestFalse(TEXT("The first border is the outside"), Paths[0].bIsHole);
			TestTrue(TEXT("The second border is the hole"), Paths[1].bIsHole);
			TestEqual(TEXT("The hole belongs to the outside border"), Paths[1].ParentIndex, 0);
		}

		Options.Approximation = EPCGUtilsContourApproximation::Simple;
		PCGUtilsVectorization::BorderFollowing(Outline, Options, Paths);
		if (TestEqual(TEXT("Simple approximation keeps two borders"), Paths.Num(), 2))
		{
			TestEqual(TEXT("Simple approximation reduces a square border to its 4 corners"), Paths[0].Points.Num(), 4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsImageSamplingTest,
	"PCGUtils.Textures.Sampling.ResampleLevelsInvertBlur",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsImageSamplingTest::RunTest(const FString&)
{
	// A 4x4 image, left half 0.2 red, right half 0.8 red.
	TArray<FLinearColor> Colors;
	for (int32 Y = 0; Y < 4; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			Colors.Add(FLinearColor(X < 2 ? 0.2f : 0.8f, 0.0f, 0.0f, 1.0f));
		}
	}

	FPCGUtilsImageSampleOptions Options;
	Options.Channel = EPCGUtilsImageChannel::Red;
	Options.MaxResolution = 2;
	FPCGUtilsGreyscaleImage Image;
	PCGUtilsImageSampling::ProcessLinearColors(Colors, 4, 4, Options, Image);
	TestEqual(TEXT("Max Resolution 2 halves a 4x4 image"), FIntPoint(Image.Width, Image.Height), FIntPoint(2, 2));
	TestTrue(TEXT("Box resample averages the left half"), FMath::IsNearlyEqual(Image.At(0, 0), 0.2f, 1e-5f));
	TestTrue(TEXT("Box resample averages the right half"), FMath::IsNearlyEqual(Image.At(1, 1), 0.8f, 1e-5f));

	Options.BlackPoint = 0.2f;
	Options.WhitePoint = 0.8f;
	Options.bInvert = true;
	PCGUtilsImageSampling::ProcessLinearColors(Colors, 4, 4, Options, Image);
	TestTrue(TEXT("Levels then invert: 0.2 becomes 1"), FMath::IsNearlyEqual(Image.At(0, 0), 1.0f, 1e-5f));
	TestTrue(TEXT("Levels then invert: 0.8 becomes 0"), FMath::IsNearlyEqual(Image.At(1, 0), 0.0f, 1e-5f));

	FPCGUtilsGreyscaleImage Flat;
	Flat.Init(8, 8, 0.6f);
	PCGUtilsImageSampling::Blur(Flat, 3.0f);
	TestTrue(TEXT("Blur leaves a flat image flat"), FMath::IsNearlyEqual(Flat.At(4, 4), 0.6f, 1e-5f));

	TestEqual(TEXT("Max Resolution keeps the aspect ratio"), PCGUtilsImageSampling::ResolveOutputSize(FIntPoint(2000, 1000), 1000), FIntPoint(1000, 500));
	TestEqual(TEXT("Max Resolution 0 keeps the native size"), PCGUtilsImageSampling::ResolveOutputSize(FIntPoint(300, 200), 0), FIntPoint(300, 200));
	return true;
}

#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsImagePathComponentTest,
	"PCGUtils.Textures.ImagePath.RetracesOnlyWhenTheTraceChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsImagePathComponentTest::RunTest(const FString&)
{
	using namespace PCGUtilsVectorizationTests;

	// A 64x64 greyscale texture holding a ring, read from its source data.
	const FPCGUtilsGreyscaleImage Ring = MakeRing(64, FVector2D(32, 32), 10.0, 20.0);
	TArray<uint8> Bytes;
	for (const float Value : Ring.Values)
	{
		Bytes.Add(Value > 0.5f ? 255 : 0);
	}
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
	Texture->SRGB = false;
	Texture->Source.Init(64, 64, 1, 1, TSF_G8, Bytes.GetData());

	UImagePathComponent* Component = NewObject<UImagePathComponent>(GetTransientPackage());
	Component->Texture = Texture;
	Component->Sampling.ReadSource = EPCGUtilsImageReadSource::SourceData;
	Component->ImageWidth = 640.0f; // 10 cm per pixel

	TestTrue(TEXT("The first rebuild traces"), Component->RebuildPaths());
	TestEqual(TEXT("One trace so far"), Component->GetTraceCount(), 1);
	TestEqual(TEXT("The ring traced as two paths"), Component->GetPathCount(), 2);
	TestEqual(TEXT("The path provider reports both paths"), IPCGPathProvider::Execute_GetPCGPathCount(Component), 2);

	bool bLocal = false, bLinear = false, bClosed = false, bHole = false;
	const TArray<FPCGPathPoint> HolePoints = IPCGPathProvider::Execute_GetPCGPathPointsAt(Component, 1, bLocal, bLinear, bClosed, bHole);
	TestTrue(TEXT("Path 1 is reported as a hole"), bHole);
	TestTrue(TEXT("Paths are closed and in local space"), bClosed && bLocal);
	double MaxRadius = 0.0;
	for (const FPCGPathPoint& Point : IPCGPathProvider::Execute_GetPCGPathPointsAt(Component, 0, bLocal, bLinear, bClosed, bHole))
	{
		MaxRadius = FMath::Max(MaxRadius, Point.Transform.GetLocation().Size2D());
	}
	TestTrue(*FString::Printf(TEXT("The outer boundary is about 200 cm out (%.1f): 20 px at 10 cm/px around the centered pivot"), MaxRadius),
		FMath::Abs(MaxRadius - 200.0) < 10.0);

	TestTrue(TEXT("An unchanged rebuild is a cache hit"), Component->RebuildPaths());
	Component->ImageWidth = 1280.0f;
	Component->Pivot = FVector2D(0.0, 0.0);
	Component->PathData.GroupID = 7;
	Component->RebuildPaths();
	TestEqual(TEXT("Placement and path data never retrace"), Component->GetTraceCount(), 1);

	Component->Vectorization.BorderFollowing.Threshold = 0.25f;
	Component->RebuildPaths();
	TestEqual(TEXT("Editing an algorithm that is not selected does not retrace"), Component->GetTraceCount(), 1);

	Component->Vectorization.MarchingSquares.IsoLevel = 0.4f;
	Component->RebuildPaths();
	TestEqual(TEXT("Editing the selected algorithm's options retraces"), Component->GetTraceCount(), 2);

	Component->Sampling.BlurRadius = 1.0f;
	Component->RebuildPaths();
	TestEqual(TEXT("Editing the sampling retraces"), Component->GetTraceCount(), 3);

	Component->Vectorization.Algorithm = EPCGUtilsImageVectorizer::CurveFit;
	Component->RebuildPaths();
	TestEqual(TEXT("Switching algorithm retraces"), Component->GetTraceCount(), 4);
	TestFalse(TEXT("Curve Fit paths are not linear"), [&]
	{
		bool bL = false, bLin = true, bC = false, bH = false;
		IPCGPathProvider::Execute_GetPCGPathPointsAt(Component, 0, bL, bLin, bC, bH);
		return bLin;
	}());

	TArray<uint8> Inverted = Bytes;
	for (uint8& Byte : Inverted) { Byte = 255 - Byte; }
	Texture->Source.Init(64, 64, 1, 1, TSF_G8, Inverted.GetData());
	Component->RebuildPaths();
	TestEqual(TEXT("New texture content retraces"), Component->GetTraceCount(), 5);

	Component->RebuildPaths(/*bForce=*/true);
	TestEqual(TEXT("Force always retraces"), Component->GetTraceCount(), 6);
	return true;
}
#endif

#endif // WITH_AUTOMATION_TESTS
