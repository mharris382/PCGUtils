// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// Deliberately only the surface-pathing header plus GeometryCore - no PCG element headers. Proves the routing
// and tracing layer is usable (and testable) without any PCG execution context, matching the constraint the
// surface-correspondence tests establish for that helper.
#include "Geometry/PCGUtilsDynMeshSurfacePathing.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"

namespace
{
	using namespace UE::Geometry;
	namespace Pathing = PCGUtilsDynMeshSurfacePathing;
	namespace Corr = PCGUtilsDynMeshSurfaceCorrespondence;

	/** Appends an NxN grid of quads spanning [Origin, Origin + (Size, Size)] to Mesh, at Z = HeightAt(x, y). */
	void AppendGrid(
		FDynamicMesh3& Mesh, int32 N, double Size, const FVector2d& Origin,
		TFunctionRef<double(double, double)> HeightAt)
	{
		TArray<int32> VIDs;
		VIDs.SetNum((N + 1) * (N + 1));
		for (int32 Y = 0; Y <= N; ++Y)
		{
			for (int32 X = 0; X <= N; ++X)
			{
				const double PX = Origin.X + Size * X / N;
				const double PY = Origin.Y + Size * Y / N;
				VIDs[Y * (N + 1) + X] = Mesh.AppendVertex(FVector3d(PX, PY, HeightAt(PX, PY)));
			}
		}
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 A = Y * (N + 1) + X;
				const int32 B = Y * (N + 1) + X + 1;
				const int32 C = (Y + 1) * (N + 1) + X + 1;
				const int32 D = (Y + 1) * (N + 1) + X;
				Mesh.AppendTriangle(VIDs[A], VIDs[B], VIDs[C]);
				Mesh.AppendTriangle(VIDs[A], VIDs[C], VIDs[D]);
			}
		}
	}

	double Flat(double, double) { return 0.0; }

	/** A symmetric ridge along Y, peaking at X = 50: routing across it must climb rather than cut through. */
	double Tent(double X, double) { return 50.0 - FMath::Abs(X - 50.0); }

	double PathLength(TConstArrayView<FVector3d> Points)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector3d::Distance(Points[Index - 1], Points[Index]);
		}
		return Length;
	}

	Corr::FMeshSurfaceProjectionResult Project(const FDynamicMeshAABBTree3& Tree, const TArray<FVector3d>& Points)
	{
		Corr::FProjectionOptions Options;
		Options.bParallel = false;
		return Corr::ProjectPoints(Tree, Points, Options);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingFlatRouteTest,
	"PCGUtils.DynMesh.SurfacePathing.RouteAcrossFlatPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingFlatRouteTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 8, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	// Anchors deliberately float above the plane: a sparse guide path should not need to sit on the geometry.
	const TArray<FVector3d> Guides = {FVector3d(10.0, 50.0, 7.0), FVector3d(90.0, 50.0, -4.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Guides);
	UTEST_TRUE("Both off-surface guide points project", Projection.AllProjected());

	Pathing::FSurfaceRouter Router(Mesh);
	const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);
	UTEST_TRUE("Both anchors inserted", Anchors[0].bValid && Anchors[1].bValid);

	TArray<FVector3d> Points;
	UTEST_TRUE("Segment solves",
		Router.RouteSegment(Anchors[0], Anchors[1], Points) == Pathing::ESurfaceRouteStatus::Ok);
	UTEST_TRUE("Path has at least two points", Points.Num() >= 2);

	for (const FVector3d& Point : Points)
	{
		UTEST_TRUE("Every routed point lies on the plane", FMath::Abs(Point.Z) < 0.1);
	}
	UTEST_TRUE("Path starts at the first anchor", FVector3d::Distance(Points[0], Anchors[0].Position) < 0.01);
	UTEST_TRUE("Path ends at the second anchor", FVector3d::Distance(Points.Last(), Anchors[1].Position) < 0.01);
	// On a flat surface the geodesic is the straight line between the projected anchors.
	UTEST_TRUE("Path is straight", FMath::Abs(PathLength(Points) - 80.0) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingCurvedRouteTest,
	"PCGUtils.DynMesh.SurfacePathing.RouteFollowsCurvedSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingCurvedRouteTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 16, 100.0, FVector2d::Zero(), Tent);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	const TArray<FVector3d> Guides = {FVector3d(5.0, 50.0, 5.0), FVector3d(95.0, 50.0, 5.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Guides);
	UTEST_TRUE("Both guide points project", Projection.AllProjected());

	Pathing::FSurfaceRouter Router(Mesh);
	const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);

	TArray<FVector3d> Points;
	UTEST_TRUE("Segment solves",
		Router.RouteSegment(Anchors[0], Anchors[1], Points) == Pathing::ESurfaceRouteStatus::Ok);

	// Straight through space would be 90 units; over the ridge it must be noticeably longer, and must actually
	// reach the crease rather than tunnelling under it.
	const double Length = PathLength(Points);
	UTEST_TRUE("Path is longer than the straight-line distance", Length > 100.0);

	double MaxHeight = 0.0;
	for (const FVector3d& Point : Points)
	{
		MaxHeight = FMath::Max(MaxHeight, Point.Z);
		// Every point must sit on the tent surface, within the tolerance of its own triangulation.
		UTEST_TRUE("Every routed point lies on the surface",
			FMath::Abs(Point.Z - Tent(Point.X, Point.Y)) < 1.0);
	}
	UTEST_TRUE("Path climbs the ridge", MaxHeight > 40.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingMultiAnchorTest,
	"PCGUtils.DynMesh.SurfacePathing.MultiAnchorRouteHasNoDuplicateJoins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingMultiAnchorTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 12, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	const TArray<FVector3d> Guides = {
		FVector3d(10.0, 10.0, 2.0),
		FVector3d(90.0, 10.0, 2.0),
		FVector3d(90.0, 90.0, 2.0),
		FVector3d(10.0, 90.0, 2.0),
	};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Guides);
	UTEST_TRUE("All guide points project", Projection.AllProjected());

	Pathing::FSurfaceRouter Router(Mesh);
	const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);
	for (const Pathing::FSurfaceAnchor& Anchor : Anchors)
	{
		UTEST_TRUE("Every anchor was inserted", Anchor.bValid);
	}
	// Distinct guide points must not collapse onto one another when several are poked into the same mesh.
	UTEST_EQUAL("Anchors are distinct vertices",
		TSet<int32>({Anchors[0].VertexID, Anchors[1].VertexID, Anchors[2].VertexID, Anchors[3].VertexID}).Num(), 4);

	TArray<FVector3d> Routed;
	TArray<FVector3d> Segment;
	for (int32 SegmentIndex = 0; SegmentIndex + 1 < Anchors.Num(); ++SegmentIndex)
	{
		UTEST_TRUE("Every segment solves",
			Router.RouteSegment(Anchors[SegmentIndex], Anchors[SegmentIndex + 1], Segment)
				== Pathing::ESurfaceRouteStatus::Ok);
		Pathing::AppendSegment(Routed, Segment, Pathing::PathJoinTolerance);
	}

	for (int32 Index = 1; Index < Routed.Num(); ++Index)
	{
		UTEST_TRUE("No coincident consecutive points anywhere in the concatenated path",
			FVector3d::Distance(Routed[Index - 1], Routed[Index]) > Pathing::PathJoinTolerance);
	}
	// Each interior anchor is where two segments meet, and must appear in the concatenated path exactly once.
	for (int32 AnchorIndex = 1; AnchorIndex + 1 < Anchors.Num(); ++AnchorIndex)
	{
		int32 Occurrences = 0;
		for (const FVector3d& Point : Routed)
		{
			if (FVector3d::Distance(Point, Anchors[AnchorIndex].Position) <= Pathing::PathJoinTolerance)
			{
				++Occurrences;
			}
		}
		UTEST_EQUAL("Interior anchor appears exactly once in the concatenated path", Occurrences, 1);
	}
	// Three 80-unit legs, joined without doubling back.
	UTEST_TRUE("Concatenated path covers all three legs", FMath::Abs(PathLength(Routed) - 240.0) < 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingDisconnectedTest,
	"PCGUtils.DynMesh.SurfacePathing.DisconnectedComponentsFailCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingDisconnectedTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 4, 100.0, FVector2d::Zero(), Flat);
	AppendGrid(Mesh, 4, 100.0, FVector2d(500.0, 0.0), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	const TArray<FVector3d> Guides = {FVector3d(50.0, 50.0, 0.0), FVector3d(550.0, 50.0, 0.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Guides);
	UTEST_TRUE("Both guide points project onto their own island", Projection.AllProjected());

	Pathing::FSurfaceRouter Router(Mesh);
	const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);

	TArray<FVector3d> Points;
	UTEST_TRUE("Routing across disconnected islands reports Disconnected",
		Router.RouteSegment(Anchors[0], Anchors[1], Points) == Pathing::ESurfaceRouteStatus::Disconnected);
	UTEST_EQUAL("Failed routing produces no points", Points.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingProjectionLimitTest,
	"PCGUtils.DynMesh.SurfacePathing.ProjectionLimitRejectsFarAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingProjectionLimitTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 4, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	Corr::FProjectionOptions Options;
	Options.bParallel = false;
	Options.MaxDistance = 10.0;
	const TArray<FVector3d> Guides = {FVector3d(20.0, 50.0, 5.0), FVector3d(80.0, 50.0, 250.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Corr::ProjectPoints(Tree, Guides, Options);

	Pathing::FSurfaceRouter Router(Mesh);
	const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);
	UTEST_TRUE("The near anchor is accepted", Anchors[0].bValid);
	UTEST_FALSE("The far anchor is rejected", Anchors[1].bValid);

	TArray<FVector3d> Points;
	UTEST_TRUE("Routing to a rejected anchor reports which end failed",
		Router.RouteSegment(Anchors[0], Anchors[1], Points) == Pathing::ESurfaceRouteStatus::EndNotOnMesh);
	UTEST_TRUE("Routing from a rejected anchor reports which end failed",
		Router.RouteSegment(Anchors[1], Anchors[0], Points) == Pathing::ESurfaceRouteStatus::StartNotOnMesh);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingTraceFlatTest,
	"PCGUtils.DynMesh.SurfacePathing.TraceFollowsDirectionOnFlatPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingTraceFlatTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 10, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	// Seed above the surface, near the -X edge, tracing towards +X. Deliberately off the y = 50 grid line, so
	// the trace crosses triangle edges transversally rather than running along one (see the stall test below).
	const TArray<FVector3d> Seeds = {FVector3d(5.0, 53.0, 12.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Seeds);
	UTEST_TRUE("The off-surface seed projects", Projection.AllProjected());

	TArray<FVector3d> Points;
	bool bReachedBoundary = false;
	UTEST_TRUE("Trace succeeds",
		Pathing::TraceSurfacePath(Mesh, Projection.Projections[0], FVector3d::UnitX(), 50.0, Points, bReachedBoundary)
			== Pathing::ESurfaceTraceStatus::Ok);

	UTEST_FALSE("A 50-unit trace from x=5 stops on distance, not on the boundary at x=100", bReachedBoundary);
	for (const FVector3d& Point : Points)
	{
		UTEST_TRUE("Every traced point stays on the plane", FMath::Abs(Point.Z) < 0.1);
		UTEST_TRUE("The trace holds its Y", FMath::Abs(Point.Y - 53.0) < 0.5);
	}
	UTEST_TRUE("The trace moves in +X", Points.Last().X > Points[0].X + 40.0);
	UTEST_TRUE("The trace covers roughly the requested length", FMath::Abs(PathLength(Points) - 50.0) < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingTraceBoundaryTest,
	"PCGUtils.DynMesh.SurfacePathing.TraceTerminatesAtMeshBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingTraceBoundaryTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 10, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	const TArray<FVector3d> Seeds = {FVector3d(50.0, 50.0, 0.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Seeds);

	TArray<FVector3d> Points;
	bool bReachedBoundary = false;
	// Far more length than the 50 units of surface available in +X.
	UTEST_TRUE("Trace succeeds",
		Pathing::TraceSurfacePath(Mesh, Projection.Projections[0], FVector3d::UnitX(), 5000.0, Points, bReachedBoundary)
			== Pathing::ESurfaceTraceStatus::Ok);
	UTEST_TRUE("The trace reports terminating on the mesh boundary", bReachedBoundary);
	UTEST_TRUE("The trace stops at the mesh edge", FMath::Abs(Points.Last().X - 100.0) < 1.0);
	UTEST_TRUE("The trace is bounded by the available surface", PathLength(Points) < 60.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingTraceStallTest,
	"PCGUtils.DynMesh.SurfacePathing.TraceStallsCompactlyOnVertexCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingTraceStallTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 10, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	// (5, 50) sits exactly on the y = 50 grid line, so tracing along +X runs straight into the vertex at
	// (10, 50). The engine tracer can stop advancing there and keep reporting that one surface point; the
	// result must still be a short, duplicate-free path rather than thousands of coincident points.
	const TArray<FVector3d> Seeds = {FVector3d(5.0, 50.0, 0.0)};
	const Corr::FMeshSurfaceProjectionResult Projection = Project(Tree, Seeds);

	TArray<FVector3d> Points;
	bool bReachedBoundary = false;
	const Pathing::ESurfaceTraceStatus Status = Pathing::TraceSurfacePath(
		Mesh, Projection.Projections[0], FVector3d::UnitX(), 50.0, Points, bReachedBoundary);

	if (Status == Pathing::ESurfaceTraceStatus::Ok)
	{
		UTEST_TRUE("A stalled trace does not accumulate coincident points", Points.Num() < 32);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			UTEST_TRUE("A stalled trace emits no coincident consecutive points",
				FVector3d::Distance(Points[Index - 1], Points[Index]) > Pathing::PathJoinTolerance);
		}
	}
	else
	{
		UTEST_TRUE("A stalled trace reports Degenerate rather than a malformed path",
			Status == Pathing::ESurfaceTraceStatus::Degenerate);
		UTEST_EQUAL("A failed trace produces no points", Points.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfacePathingTraceMultipleSeedsTest,
	"PCGUtils.DynMesh.SurfacePathing.TraceProducesOnePathPerSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfacePathingTraceMultipleSeedsTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	AppendGrid(Mesh, 10, 100.0, FVector2d::Zero(), Flat);
	FDynamicMeshAABBTree3 Tree(&Mesh, true);

	const TArray<FVector3d> Seeds = {
		FVector3d(20.0, 20.0, 0.0),
		FVector3d(20.0, 80.0, 0.0),
		FVector3d(1000.0, 1000.0, 0.0), // beyond the projection limit below: contributes no path at all
	};
	Corr::FProjectionOptions Options;
	Options.bParallel = false;
	Options.MaxDistance = 50.0;
	const Corr::FMeshSurfaceProjectionResult Projection = Corr::ProjectPoints(Tree, Seeds, Options);

	TArray<TArray<FVector3d>> Paths;
	for (int32 SeedIndex = 0; SeedIndex < Seeds.Num(); ++SeedIndex)
	{
		TArray<FVector3d> Points;
		bool bReachedBoundary = false;
		if (Pathing::TraceSurfacePath(Mesh, Projection.Projections[SeedIndex], FVector3d::UnitY(), 30.0,
				Points, bReachedBoundary) == Pathing::ESurfaceTraceStatus::Ok)
		{
			Paths.Add(MoveTemp(Points));
		}
	}

	UTEST_EQUAL("The two interior seeds each produced their own path", Paths.Num(), 2);
	UTEST_TRUE("The paths are independent, not concatenated",
		FVector3d::Distance(Paths[0][0], Paths[1][0]) > 50.0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
