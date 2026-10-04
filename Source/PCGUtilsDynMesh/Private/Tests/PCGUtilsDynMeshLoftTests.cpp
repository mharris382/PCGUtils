// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// Deliberately only the loft header plus GeometryCore - no PCG element headers. The generator has to be usable
// (and testable) without a PCG execution context, matching the surface-pathing tests.
#include "Geometry/PCGUtilsDynMeshLoft.h"

#include "Algo/Reverse.h"
#include "DynamicMesh/DynamicMesh3.h"

namespace
{
	namespace Loft = PCGUtilsDynMeshLoft;

	int32 CountLoftBoundaryEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.BoundaryEdgeIndicesItr())
		{
			(void)EdgeID;
			++Count;
		}
		return Count;
	}

	/** A deliberately uneven rail: the seam contract only means something if spacing is not uniform. */
	TArray<FVector3d> MakeUnevenLoftRail(double Y, double Z)
	{
		return {FVector3d(0, Y, Z), FVector3d(10, Y, Z), FVector3d(35, Y, Z), FVector3d(40, Y, Z), FVector3d(100, Y, Z)};
	}

	TArray<FVector3d> MakeLoftSquare(double HalfSize, double Z, bool bReversed)
	{
		TArray<FVector3d> Square = {
			FVector3d(-HalfSize, -HalfSize, Z), FVector3d(HalfSize, -HalfSize, Z),
			FVector3d(HalfSize, HalfSize, Z), FVector3d(-HalfSize, HalfSize, Z)};
		if (bReversed)
		{
			Algo::Reverse(Square);
		}
		return Square;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftOpenSeamTest,
	"PCGUtils.DynMesh.Loft.OpenLoftKeepsRailPointsExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftOpenSeamTest::RunTest(const FString&)
{
	const TArray<FVector3d> RailA = MakeUnevenLoftRail(0.0, 0.0);
	const TArray<FVector3d> RailB = MakeUnevenLoftRail(50.0, -20.0);

	Loft::FLoftOptions Options;
	Options.NumRows = 4;

	UE::Geometry::FDynamicMesh3 Mesh;
	Loft::FLoftResult Result;
	FString Error;
	UTEST_TRUE("An open loft between matching rails builds", Loft::BuildLoft(RailA, RailB, Options, Mesh, Result, Error));

	UTEST_EQUAL("One vertex per rail point per row", Mesh.VertexCount(), 5 * 5);
	UTEST_EQUAL("Two triangles per quad", Mesh.TriangleCount(), 4 * 4 * 2);
	UTEST_EQUAL("An open strip has one boundary loop of 2*(columns-1) + 2*rows edges", CountLoftBoundaryEdges(Mesh), 2 * 4 + 2 * 4);

	for (int32 Column = 0; Column < RailA.Num(); ++Column)
	{
		// Exact equality on purpose: the seam can only be welded if these are the same doubles.
		UTEST_TRUE("Row 0 is Rail A verbatim", Mesh.GetVertex(Result.RailAVertexIDs[Column]) == RailA[Column]);
		UTEST_TRUE("The last row is Rail B verbatim", Mesh.GetVertex(Result.RailBVertexIDs[Column]) == RailB[Column]);
	}

	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		UTEST_TRUE("Faces point up by default", Mesh.GetTriNormal(TriangleID).Z > 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftClosedTest,
	"PCGUtils.DynMesh.Loft.ClosedLoftIsARing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftClosedTest::RunTest(const FString&)
{
	// Both windings must come out facing up: the winding of an authored loop is not something a user controls.
	for (const bool bReversed : {false, true})
	{
		const TArray<FVector3d> Crest = MakeLoftSquare(50.0, 30.0, bReversed);
		const TArray<FVector3d> Toe = MakeLoftSquare(80.0, 0.0, bReversed);

		Loft::FLoftOptions Options;
		Options.NumRows = 3;
		Options.bClosed = true;

		UE::Geometry::FDynamicMesh3 Mesh;
		Loft::FLoftResult Result;
		FString Error;
		UTEST_TRUE("A closed loft builds", Loft::BuildLoft(Crest, Toe, Options, Mesh, Result, Error));
		UTEST_EQUAL("A closed loft does not duplicate its first column", Mesh.VertexCount(), 4 * 4);
		UTEST_EQUAL("Every column including the wrap has quads", Mesh.TriangleCount(), 4 * 3 * 2);
		UTEST_EQUAL("A ring has exactly its two rails as boundary", CountLoftBoundaryEdges(Mesh), 4 + 4);
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			UTEST_TRUE("Faces point up for either winding", Mesh.GetTriNormal(TriangleID).Z > 0.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftProfileTest,
	"PCGUtils.DynMesh.Loft.ProfileShapesHeightOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftProfileTest::RunTest(const FString&)
{
	const TArray<FVector3d> RailA = {FVector3d(0, 0, 0), FVector3d(100, 0, 0)};
	const TArray<FVector3d> RailB = {FVector3d(0, 80, -40), FVector3d(100, 80, -40)};

	Loft::FLoftOptions Options;
	Options.NumRows = 4;
	Options.HeightProfile = [](double T) { return T * T; };

	UE::Geometry::FDynamicMesh3 Mesh;
	Loft::FLoftResult Result;
	FString Error;
	UTEST_TRUE("A profiled loft builds", Loft::BuildLoft(RailA, RailB, Options, Mesh, Result, Error));

	// Vertices are appended row by row, two per row; row 2 is the halfway row.
	const FVector3d Halfway = Mesh.GetVertex(Result.RailAVertexIDs[0] + 2 * 2);
	UTEST_EQUAL_TOLERANCE("Sideways position stays linear", Halfway.Y, 40.0, 1e-9);
	UTEST_EQUAL_TOLERANCE("Height follows the profile: 0.5^2 of the drop", Halfway.Z, -10.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftOffsetTest,
	"PCGUtils.DynMesh.Loft.OffsetRailGoesOutwardAndMiters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftOffsetTest::RunTest(const FString&)
{
	for (const bool bReversed : {false, true})
	{
		const TArray<FVector3d> Square = MakeLoftSquare(50.0, 0.0, bReversed);

		Loft::FOffsetOptions Options;
		Options.bClosed = true;
		Options.Distance = 10.0;
		Options.Height = -5.0;

		TArray<FVector3d> Offset;
		const Loft::FOffsetResult Result = Loft::OffsetRail(Square, Options, Offset);
		UTEST_EQUAL("Offsetting keeps one point per point", Offset.Num(), Square.Num());
		UTEST_EQUAL("A convex outline offset outward never inverts", Result.NumInvertedSegments, 0);
		for (int32 Index = 0; Index < Square.Num(); ++Index)
		{
			// A square corner miters to the corner of the larger square.
			UTEST_EQUAL_TOLERANCE("Corner X is mitered outward", FMath::Abs(Offset[Index].X), 60.0, 1e-9);
			UTEST_EQUAL_TOLERANCE("Corner Y is mitered outward", FMath::Abs(Offset[Index].Y), 60.0, 1e-9);
			UTEST_EQUAL_TOLERANCE("Height offset is applied along up", Offset[Index].Z, -5.0, 1e-9);
			UTEST_TRUE("The offset corner stays on its own corner's side",
				FMath::Sign(Offset[Index].X) == FMath::Sign(Square[Index].X) &&
				FMath::Sign(Offset[Index].Y) == FMath::Sign(Square[Index].Y));
		}
	}

	// Offsetting inward by more than the square's half-size folds every side over.
	Loft::FOffsetOptions Inward;
	Inward.bClosed = true;
	Inward.Distance = -80.0;
	Inward.MiterLimit = 4.0;
	TArray<FVector3d> Folded;
	const Loft::FOffsetResult FoldedResult = Loft::OffsetRail(MakeLoftSquare(50.0, 0.0, false), Inward, Folded);
	UTEST_EQUAL("An offset larger than the shape reports every segment as inverted", FoldedResult.NumInvertedSegments, 4);
	UTEST_EQUAL("And names the first one", FoldedResult.FirstInvertedSegment, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftCorrespondenceTest,
	"PCGUtils.DynMesh.Loft.AlignAndResamplePairIndependentRails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftCorrespondenceTest::RunTest(const FString&)
{
	// Rail B runs the other way and has a different point count - the two-independently-drawn-curves case.
	const TArray<FVector3d> RailA = MakeUnevenLoftRail(0.0, 0.0);
	TArray<FVector3d> RailB = {FVector3d(100, 50, 0), FVector3d(50, 50, 0), FVector3d(0, 50, 0)};

	Loft::AlignRail(RailA, false, FVector3d::UnitZ(), RailB);
	UTEST_EQUAL_TOLERANCE("A reversed open rail is turned to run with the reference", RailB[0].X, 0.0, 1e-9);

	TArray<FVector3d> Resampled;
	Loft::ResampleByArcLength(RailB, false, RailA.Num(), Resampled);
	UTEST_EQUAL("Resampling yields the requested count", Resampled.Num(), RailA.Num());
	UTEST_EQUAL_TOLERANCE("An open rail keeps its first point", Resampled[0].X, 0.0, 1e-9);
	UTEST_EQUAL_TOLERANCE("An open rail keeps its last point", Resampled.Last().X, 100.0, 1e-9);
	UTEST_EQUAL_TOLERANCE("Samples are evenly spaced by arc length", Resampled[2].X, 50.0, 1e-9);

	// A closed rail that starts at a different corner and winds the other way.
	const TArray<FVector3d> Crest = MakeLoftSquare(50.0, 0.0, false);
	TArray<FVector3d> Toe = MakeLoftSquare(80.0, 0.0, true);
	Loft::AlignRail(Crest, true, FVector3d::UnitZ(), Toe);
	for (int32 Index = 0; Index < Crest.Num(); ++Index)
	{
		UTEST_TRUE("Each toe corner lines up with its crest corner",
			FMath::Sign(Toe[Index].X) == FMath::Sign(Crest[Index].X) &&
			FMath::Sign(Toe[Index].Y) == FMath::Sign(Crest[Index].Y));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshLoftValidationTest,
	"PCGUtils.DynMesh.Loft.RejectsMismatchedRailsWithoutTouchingTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshLoftValidationTest::RunTest(const FString&)
{
	const TArray<FVector3d> RailA = MakeUnevenLoftRail(0.0, 0.0);
	const TArray<FVector3d> ShortRail = {FVector3d(0, 50, 0), FVector3d(100, 50, 0)};

	UE::Geometry::FDynamicMesh3 Mesh;
	Loft::FLoftResult Result;
	FString Error;
	UTEST_FALSE("Rails of different counts are rejected", Loft::BuildLoft(RailA, ShortRail, Loft::FLoftOptions(), Mesh, Result, Error));
	UTEST_TRUE("The error names both counts", Error.Contains(TEXT("2")) && Error.Contains(TEXT("5")));
	UTEST_EQUAL("A rejected loft leaves the mesh empty", Mesh.VertexCount(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
