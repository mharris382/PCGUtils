// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// Deliberately only the correspondence header plus GeometryCore — no Painter, Static Mesh Component, or PCG
// element headers. Proves the helper is usable without depending on any of those.
#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace
{
	using namespace UE::Geometry;
	namespace Corr = PCGUtilsDynMeshSurfaceCorrespondence;

	/** A flat NxN grid quad in the z=Z plane spanning [0,Size]^2, primary color overlay = f(local position). */
	void BuildColoredGrid(
		FDynamicMesh3& Mesh, int32 N, double Size, double Z,
		TFunctionRef<FVector4f(const FVector3d&)> ColorAt)
	{
		Mesh = FDynamicMesh3();
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnablePrimaryColors();
		FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();

		TArray<int32> VIDs;
		TArray<int32> EIDs;
		VIDs.SetNum((N + 1) * (N + 1));
		EIDs.SetNum((N + 1) * (N + 1));
		for (int32 Y = 0; Y <= N; ++Y)
		{
			for (int32 X = 0; X <= N; ++X)
			{
				const FVector3d P(Size * X / N, Size * Y / N, Z);
				const int32 VID = Mesh.AppendVertex(P);
				VIDs[Y * (N + 1) + X] = VID;
				EIDs[Y * (N + 1) + X] = Colors->AppendElement(ColorAt(P));
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
				const int32 T0 = Mesh.AppendTriangle(VIDs[A], VIDs[B], VIDs[C]);
				const int32 T1 = Mesh.AppendTriangle(VIDs[A], VIDs[C], VIDs[D]);
				Colors->SetTriangle(T0, FIndex3i(EIDs[A], EIDs[B], EIDs[C]));
				Colors->SetTriangle(T1, FIndex3i(EIDs[A], EIDs[C], EIDs[D]));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfaceCorrespondenceGradientTest,
	"PCGUtils.DynMesh.SurfaceCorrespondence.GradientTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfaceCorrespondenceGradientTest::RunTest(const FString&)
{
	FDynamicMesh3 Source;
	// R encodes X in [0,1], G encodes Y in [0,1]; B/A constant.
	BuildColoredGrid(Source, 8, 100.0, 0.0, [](const FVector3d& P)
	{
		return FVector4f((float)(P.X / 100.0), (float)(P.Y / 100.0), 0.25f, 1.0f);
	});
	FDynamicMeshAABBTree3 Tree(&Source, true);

	TArray<FVector3d> Dest = {
		FVector3d(10.0, 10.0, 3.0),   // above the surface
		FVector3d(50.0, 25.0, -2.0),
		FVector3d(90.0, 80.0, 0.0),
	};
	const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectPoints(Tree, Dest);

	TestEqual(TEXT("Every destination point projected"), Result.NumProjected, 3);
	TestEqual(TEXT("No failures"), Result.NumFailed, 0);
	TestTrue(TEXT("AllProjected"), Result.AllProjected());

	for (int32 Index = 0; Index < Dest.Num(); ++Index)
	{
		const Corr::FMeshSurfaceProjection& Projection = Result.Projections[Index];
		TestTrue(TEXT("Projection is valid"), Projection.bProjected);
		const FVector3d& B = Projection.BarycentricCoordinates;
		TestTrue(TEXT("Barycentric coordinates sum to 1"),
			FMath::IsNearlyEqual(B.X + B.Y + B.Z, 1.0, 1e-4));

		FVector4f Color;
		TestTrue(TEXT("Color overlay sampled"),
			Corr::SampleColorOverlay(Projection, *Source.Attributes()->PrimaryColors(), Color));
		TestTrue(TEXT("Interpolated R follows the X gradient"),
			FMath::IsNearlyEqual(Color.X, (float)(Dest[Index].X / 100.0), 1e-3f));
		TestTrue(TEXT("Interpolated G follows the Y gradient"),
			FMath::IsNearlyEqual(Color.Y, (float)(Dest[Index].Y / 100.0), 1e-3f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfaceCorrespondenceEdgeCasesTest,
	"PCGUtils.DynMesh.SurfaceCorrespondence.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfaceCorrespondenceEdgeCasesTest::RunTest(const FString&)
{
	// Empty source -> unavailable, everything fails, no crash.
	{
		FDynamicMesh3 Empty;
		FDynamicMeshAABBTree3 Tree(&Empty, true);
		const TArray<FVector3d> Dest = { FVector3d::ZeroVector, FVector3d(1, 2, 3) };
		const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectPoints(Tree, Dest);
		TestTrue(TEXT("Empty source flagged unavailable"), Result.bSourceUnavailable);
		TestEqual(TEXT("Empty source: all fail"), Result.NumFailed, 2);
		TestEqual(TEXT("Empty source: none projected"), Result.NumProjected, 0);
	}

	FDynamicMesh3 Source;
	BuildColoredGrid(Source, 4, 100.0, 0.0, [](const FVector3d&) { return FVector4f(1, 1, 1, 1); });
	FDynamicMeshAABBTree3 Tree(&Source, true);

	// Sparse destination vertex IDs must not read geometry out of bounds.
	{
		FDynamicMesh3 Dest;
		BuildColoredGrid(Dest, 3, 100.0, 5.0, [](const FVector3d&) { return FVector4f(0, 0, 0, 1); });
		const int32 RemovedVID = 5;
		Dest.RemoveVertex(RemovedVID);
		const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectMeshVertices(Tree, Dest);
		TestEqual(TEXT("Result is indexed by destination vertex ID"), Result.Projections.Num(), Dest.MaxVertexID());
		TestFalse(TEXT("Removed vertex ID left unprojected"), Result.Projections[RemovedVID].bProjected);
		int32 LiveProjected = 0;
		for (const int32 VID : Dest.VertexIndicesItr())
		{
			LiveProjected += Result.Projections[VID].bProjected ? 1 : 0;
		}
		TestEqual(TEXT("Every live destination vertex projected"), LiveProjected, Dest.VertexCount());
	}

	// MaxDistance rejects a far point.
	{
		Corr::FProjectionOptions Options;
		Options.MaxDistance = 10.0;
		const TArray<FVector3d> Dest = { FVector3d(50.0, 50.0, 1000.0) };
		const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectPoints(Tree, Dest, Options);
		TestEqual(TEXT("Far point rejected by MaxDistance"), Result.NumFailed, 1);
	}

	// A destination->source transform brings offset points back onto the surface.
	{
		Corr::FProjectionOptions Options;
		Options.DestinationToSource = FTransform(FVector(0.0, 0.0, -500.0));
		const TArray<FVector3d> DestInOwnSpace = { FVector3d(50.0, 50.0, 500.0) };
		const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectPoints(Tree, DestInOwnSpace, Options);
		TestEqual(TEXT("Transformed point projects"), Result.NumProjected, 1);
		TestTrue(TEXT("Transformed point lands on the surface"),
			Result.Projections[0].DistanceSquared < 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSurfaceCorrespondenceDisconnectedPiecesTest,
	"PCGUtils.DynMesh.SurfaceCorrespondence.DisconnectedPieces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSurfaceCorrespondenceDisconnectedPiecesTest::RunTest(const FString&)
{
	// Two spatially separated quads, each a flat but distinct color.
	FDynamicMesh3 Source;
	Source.EnableAttributes();
	Source.Attributes()->EnablePrimaryColors();
	FDynamicMeshColorOverlay* Colors = Source.Attributes()->PrimaryColors();

	auto AddQuad = [&Source, Colors](const FVector3d& Origin, const FVector4f& Color)
	{
		const int32 V0 = Source.AppendVertex(Origin);
		const int32 V1 = Source.AppendVertex(Origin + FVector3d(10, 0, 0));
		const int32 V2 = Source.AppendVertex(Origin + FVector3d(10, 10, 0));
		const int32 V3 = Source.AppendVertex(Origin + FVector3d(0, 10, 0));
		const int32 E0 = Colors->AppendElement(Color);
		const int32 E1 = Colors->AppendElement(Color);
		const int32 E2 = Colors->AppendElement(Color);
		const int32 E3 = Colors->AppendElement(Color);
		const int32 T0 = Source.AppendTriangle(V0, V1, V2);
		const int32 T1 = Source.AppendTriangle(V0, V2, V3);
		Colors->SetTriangle(T0, FIndex3i(E0, E1, E2));
		Colors->SetTriangle(T1, FIndex3i(E0, E2, E3));
	};
	AddQuad(FVector3d(0, 0, 0), FVector4f(1, 0, 0, 1));
	AddQuad(FVector3d(1000, 0, 0), FVector4f(0, 1, 0, 1));

	FDynamicMeshAABBTree3 Tree(&Source, true);

	const TArray<FVector3d> Dest = { FVector3d(5, 5, 2), FVector3d(1005, 5, -2) };
	const Corr::FMeshSurfaceProjectionResult Result = Corr::ProjectPoints(Tree, Dest);

	FVector4f NearFirst, NearSecond;
	Corr::SampleColorOverlay(Result.Projections[0], *Colors, NearFirst);
	Corr::SampleColorOverlay(Result.Projections[1], *Colors, NearSecond);

	TestTrue(TEXT("Point over piece A picks up piece A's color"), NearFirst.Equals(FVector4f(1, 0, 0, 1)));
	TestTrue(TEXT("Point over piece B picks up piece B's color"), NearSecond.Equals(FVector4f(0, 1, 0, 1)));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
