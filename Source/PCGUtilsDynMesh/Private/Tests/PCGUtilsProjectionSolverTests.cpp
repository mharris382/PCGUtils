// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Geometry/PCGUtilsProjectionEnvironment.h"
#include "Geometry/PCGUtilsProjectionSolver.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/GridBoxMeshGenerator.h"

/**
 * The solver and the environments, with no PCG, no Geometry Collection and no world in sight.
 *
 * That is the point of the layering: settling behaviour becomes arithmetic that can be asserted exactly, rather
 * than something only observable by looking at a level.
 */
namespace PCGUtilsProjectionSolverTests
{
	using namespace PCGUtilsProjectionSolver;

	constexpr double Tolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

	const FVector Down(0.0, 0.0, -1.0);

	/** Traces a set of positions against an environment and returns them ready to solve. */
	TArray<FSupportSample> TraceAll(
		const IPCGUtilsProjectionEnvironment& Environment,
		TConstArrayView<FVector> Positions,
		const FVector& Direction,
		double MaxDistance = 10000.0,
		double StartOffset = 10.0)
	{
		TArray<FSupportSample> Samples;
		for (const FVector& Position : Positions)
		{
			FSupportSample Sample;
			Sample.Position = Position;
			if (Environment.Trace(
				Position - Direction * StartOffset, Position + Direction * MaxDistance,
				Sample.HitLocation, Sample.HitNormal))
			{
				Sample.Travel = FVector::DotProduct(Sample.HitLocation - Position, Direction);
				Sample.bHit = true;
			}
			Samples.Add(Sample);
		}
		return Samples;
	}
}

/**
 * A body rests on its first contact, not its last.
 *
 * This is the one sign error in the solver that would still look plausible in review: taking the maximum travel
 * sinks a slab until its highest corner touches. A tilted set of samples over a flat plane distinguishes them
 * unambiguously.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectionSolverFirstContactTest,
	"PCGUtils.Projection.Solver.RestsOnFirstContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectionSolverFirstContactTest::RunTest(const FString&)
{
	using namespace PCGUtilsProjectionSolverTests;

	const FPCGUtilsPlaneProjectionEnvironment Ground(FVector::ZeroVector, FVector::UpVector);

	// Four corners of a slab, tilted: one corner 100 above the plane, the opposite one 400 above.
	const TArray<FVector> Corners = {
		FVector(-50.0, -50.0, 100.0),
		FVector( 50.0, -50.0, 200.0),
		FVector(-50.0,  50.0, 300.0),
		FVector( 50.0,  50.0, 400.0)};

	const TArray<FSupportSample> Samples = TraceAll(Ground, Corners, Down);
	TestEqual(TEXT("Every corner found the plane"), Samples.Num(), 4);
	for (const FSupportSample& Sample : Samples)
	{
		TestTrue(TEXT("Sample hit"), Sample.bHit);
	}

	FSolveSettings Settings;
	Settings.Direction = Down;
	const FSolveResult Result = Solve(Samples, Settings);

	TestTrue(TEXT("Solved"), Result.bSolved);
	TestEqual(TEXT("All four contributed"), Result.NumHits, 4);

	// The lowest corner is 100 above the plane, so the slab falls 100 - not 400, which is what taking the
	// maximum would give and would bury three of its corners.
	TestEqual(TEXT("Travelled to the first contact, not the last"), Result.Travel, 100.0, Tolerance);
	TestTrue(TEXT("The delta is that translation along the direction"),
		Result.Delta.GetTranslation().Equals(FVector(0.0, 0.0, -100.0), Tolerance));
	TestEqual(TEXT("Spread reports how badly it fits"), Result.SupportSpread, 300.0, Tolerance);

	// And after applying it, nothing is below the plane while something is exactly on it.
	double LowestAfter = TNumericLimits<double>::Max();
	for (const FVector& Corner : Corners)
	{
		LowestAfter = FMath::Min(LowestAfter, (Corner + Result.Delta.GetTranslation()).Z);
	}
	TestEqual(TEXT("The lowest corner ends exactly on the surface"), LowestAfter, 0.0, Tolerance);

	return true;
}

/** A body that starts buried is pushed back out, because travel is signed. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectionSolverBuriedTest,
	"PCGUtils.Projection.Solver.LiftsBuriedBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectionSolverBuriedTest::RunTest(const FString&)
{
	using namespace PCGUtilsProjectionSolverTests;

	const FPCGUtilsPlaneProjectionEnvironment Ground(FVector::ZeroVector, FVector::UpVector);

	// Both corners below the plane. Without a start offset the traces would begin underground and find nothing
	// on the way down, so this also pins that the offset is what makes recovery possible.
	const TArray<FVector> Corners = {FVector(-50.0, 0.0, -30.0), FVector(50.0, 0.0, -10.0)};

	const TArray<FSupportSample> Samples = TraceAll(Ground, Corners, Down, /*MaxDistance=*/10000.0,
		/*StartOffset=*/100.0);
	for (const FSupportSample& Sample : Samples)
	{
		TestTrue(TEXT("A buried sample still finds the surface behind it"), Sample.bHit);
	}

	FSolveSettings Settings;
	Settings.Direction = Down;
	const FSolveResult Result = Solve(Samples, Settings);

	TestTrue(TEXT("Solved"), Result.bSolved);

	// The minimum is -30, the *deepest* corner, so the body rises by 30 and comes fully clear. Taking the
	// shallowest instead would lift it by 10 and leave the other corner 20 units underground - which is the
	// same "first contact wins" rule as the falling case, just with the contact already behind the sample.
	TestEqual(TEXT("Travel is negative - the body moved against the direction"), Result.Travel, -30.0, Tolerance);
	TestTrue(TEXT("Lifted by its deepest penetration"),
		Result.Delta.GetTranslation().Equals(FVector(0.0, 0.0, 30.0), Tolerance));

	// The property that actually matters: nothing is left below the surface, and something is exactly on it.
	double LowestAfter = TNumericLimits<double>::Max();
	for (const FVector& Corner : Corners)
	{
		LowestAfter = FMath::Min(LowestAfter, (Corner + Result.Delta.GetTranslation()).Z);
	}
	TestEqual(TEXT("The deepest corner ends exactly on the surface"), LowestAfter, 0.0, Tolerance);

	return true;
}

/** Samples that missed contribute nothing, and an all-miss solve asks the caller to leave the body alone. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectionSolverMissTest,
	"PCGUtils.Projection.Solver.IgnoresMisses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectionSolverMissTest::RunTest(const FString&)
{
	using namespace PCGUtilsProjectionSolverTests;

	FSolveSettings Settings;
	Settings.Direction = Down;

	// Nothing hit at all.
	{
		TArray<FSupportSample> Samples;
		Samples.AddDefaulted(3);
		const FSolveResult Result = Solve(Samples, Settings);
		TestFalse(TEXT("An all-miss solve is not solved"), Result.bSolved);
		TestEqual(TEXT("And reports no hits"), Result.NumHits, 0);
		TestTrue(TEXT("And leaves the delta at identity"), Result.Delta.Equals(FTransform::Identity));
	}

	// One hit among misses: the miss must not drag the answer anywhere.
	{
		TArray<FSupportSample> Samples;
		Samples.AddDefaulted(2);
		FSupportSample Hit;
		Hit.Position = FVector(0.0, 0.0, 250.0);
		Hit.HitLocation = FVector(0.0, 0.0, 0.0);
		Hit.Travel = 250.0;
		Hit.bHit = true;
		Samples.Add(Hit);

		const FSolveResult Result = Solve(Samples, Settings);
		TestTrue(TEXT("One hit is enough to solve"), Result.bSolved);
		TestEqual(TEXT("Only the hit counted"), Result.NumHits, 1);
		TestEqual(TEXT("And it set the travel"), Result.Travel, 250.0, Tolerance);
		TestEqual(TEXT("A single contact has no spread"), Result.SupportSpread, 0.0, Tolerance);
	}

	return true;
}

/** An inclined plane settles the body on its downhill corner, and the maximum distance is respected. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectionSolverInclineTest,
	"PCGUtils.Projection.Solver.InclinedSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectionSolverInclineTest::RunTest(const FString&)
{
	using namespace PCGUtilsProjectionSolverTests;

	// A 45-degree slope through the origin: the plane z = -x, whose normal is (1,0,1) normalised.
	const FPCGUtilsPlaneProjectionEnvironment Slope(
		FVector::ZeroVector, FVector(1.0, 0.0, 1.0).GetSafeNormal());

	const TArray<FVector> Corners = {FVector(-100.0, 0.0, 500.0), FVector(100.0, 0.0, 500.0)};
	const TArray<FSupportSample> Samples = TraceAll(Slope, Corners, Down);

	FSolveSettings Settings;
	Settings.Direction = Down;
	const FSolveResult Result = Solve(Samples, Settings);
	TestTrue(TEXT("Solved on a slope"), Result.bSolved);

	// Ground under x=-100 is at z=100, under x=+100 it is at z=-100. From z=500 those are drops of 400 and 600,
	// so the uphill corner lands first and the body falls 400.
	TestEqual(TEXT("Rests on the uphill corner"), Result.Travel, 400.0, Tolerance);

	for (const FVector& Corner : Corners)
	{
		const FVector Settled = Corner + Result.Delta.GetTranslation();
		// Plane is z = -x, so a point is above it when z + x > 0. Nothing may end up below.
		TestTrue(TEXT("No corner ended below the slope"), Settled.Z + Settled.X >= -Tolerance);
	}

	// Too short a trace finds nothing, and the body is left alone rather than dropped part way.
	const TArray<FSupportSample> Short = TraceAll(Slope, Corners, Down, /*MaxDistance=*/50.0);
	TestFalse(TEXT("A trace shorter than the gap misses"), Solve(Short, Settings).bSolved);

	return true;
}

/** The Dynamic Mesh environment agrees with the analytic one where they describe the same surface. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectionDynMeshEnvironmentTest,
	"PCGUtils.Projection.Environment.DynMeshMatchesPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectionDynMeshEnvironmentTest::RunTest(const FString&)
{
	using namespace PCGUtilsProjectionSolverTests;

	// A wide, flat box whose top face sits exactly at z = 0.
	UE::Geometry::FGridBoxMeshGenerator Generator;
	Generator.Box = UE::Geometry::FOrientedBox3d(
		FVector3d(0.0, 0.0, -50.0), FVector3d(500.0, 500.0, 50.0));
	Generator.EdgeVertices = UE::Geometry::FIndex3i(1, 1, 1);
	Generator.Generate();
	const UE::Geometry::FDynamicMesh3 Mesh(&Generator);

	const FPCGUtilsDynMeshProjectionEnvironment MeshEnvironment(Mesh, FTransform::Identity);
	if (!TestTrue(TEXT("The mesh environment built a tree"), MeshEnvironment.IsUsable()))
	{
		return false;
	}

	const TArray<FVector> Corners = {
		FVector(-100.0, -100.0, 300.0),
		FVector( 100.0, -100.0, 300.0),
		FVector(-100.0,  100.0, 300.0),
		FVector( 100.0,  100.0, 300.0)};

	FSolveSettings Settings;
	Settings.Direction = Down;

	const FSolveResult FromMesh = Solve(TraceAll(MeshEnvironment, Corners, Down), Settings);
	TestTrue(TEXT("Solved against the mesh"), FromMesh.bSolved);
	TestEqual(TEXT("All four corners hit the top face"), FromMesh.NumHits, 4);
	TestEqual(TEXT("Fell exactly to the top face"), FromMesh.Travel, 300.0, 0.01);

	const FPCGUtilsPlaneProjectionEnvironment PlaneEnvironment(FVector::ZeroVector, FVector::UpVector);
	const FSolveResult FromPlane = Solve(TraceAll(PlaneEnvironment, Corners, Down), Settings);
	TestTrue(TEXT("The two environments agree on a flat surface"),
		FromMesh.Delta.GetTranslation().Equals(FromPlane.Delta.GetTranslation(), 0.01));

	// The normal must point back at where the ray came from, so a future alignment pass tilts the right way.
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::ZeroVector;
	TestTrue(TEXT("Traced the mesh"), MeshEnvironment.Trace(
		FVector(0.0, 0.0, 300.0), FVector(0.0, 0.0, -300.0), HitLocation, HitNormal));
	TestTrue(TEXT("The hit normal faces the incoming ray"), HitNormal.Z > 0.0);

	// Outside the box entirely: a miss, not a clamp to the nearest edge.
	TestFalse(TEXT("A ray beside the mesh misses"), MeshEnvironment.Trace(
		FVector(5000.0, 0.0, 300.0), FVector(5000.0, 0.0, -300.0), HitLocation, HitNormal));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
