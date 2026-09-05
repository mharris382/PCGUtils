// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace
{
	using namespace UE::Geometry;
	namespace Corr = PCGUtilsDynMeshSurfaceCorrespondence;

	/** A single painted quad in z=0 spanning [0,100]^2, each corner a caller-chosen color. */
	void BuildPaintedQuad(FDynamicMesh3& Mesh, const FVector4f& Painted)
	{
		Mesh = FDynamicMesh3();
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnablePrimaryColors();
		FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();

		const int32 V0 = Mesh.AppendVertex(FVector3d(0, 0, 0));
		const int32 V1 = Mesh.AppendVertex(FVector3d(100, 0, 0));
		const int32 V2 = Mesh.AppendVertex(FVector3d(100, 100, 0));
		const int32 V3 = Mesh.AppendVertex(FVector3d(0, 100, 0));
		const int32 E0 = Colors->AppendElement(Painted);
		const int32 E1 = Colors->AppendElement(Painted);
		const int32 E2 = Colors->AppendElement(Painted);
		const int32 E3 = Colors->AppendElement(Painted);
		const int32 T0 = Mesh.AppendTriangle(V0, V1, V2);
		const int32 T1 = Mesh.AppendTriangle(V0, V2, V3);
		Colors->SetTriangle(T0, FIndex3i(E0, E1, E2));
		Colors->SetTriangle(T1, FIndex3i(E0, E2, E3));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterLODChannelPreservationTest,
	"PCGUtils.Painter.LODTransfer.ChannelPreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterLODChannelPreservationTest::RunTest(const FString&)
{
	// Painted LOD0 surface: solid red, everything else zero.
	FDynamicMesh3 Source;
	BuildPaintedQuad(Source, FVector4f(1.0f, 0.0f, 0.0f, 0.0f));
	FDynamicMeshAABBTree3 Tree(&Source, true);

	// Lower-LOD vertices, each carrying a distinctive existing GBA the transfer must not touch when only R is written.
	const TArray<FVector3d> DestPoints = {
		FVector3d(25.0, 25.0, 0.0),
		FVector3d(75.0, 50.0, 0.0),
		FVector3d(10.0, 90.0, 0.0),
	};
	const Corr::FMeshSurfaceProjectionResult Projection = Corr::ProjectPoints(Tree, DestPoints);
	TestTrue(TEXT("All lower-LOD vertices projected"), Projection.AllProjected());

	const FDynamicMeshColorOverlay& Colors = *Source.Attributes()->PrimaryColors();

	// --- Paint only R ---------------------------------------------------------------------------------------
	{
		TArray<FVector4f> DestColors = {
			FVector4f(0.10f, 0.20f, 0.30f, 0.40f),
			FVector4f(0.11f, 0.21f, 0.31f, 0.41f),
			FVector4f(0.12f, 0.22f, 0.32f, 0.42f),
		};
		const TArray<FVector4f> Original = DestColors;

		const int32 Modified = Corr::TransferColorChannels(
			Projection, Colors, Corr::EColorChannelBits::R, DestColors);
		TestEqual(TEXT("Every projected entry modified"), Modified, 3);

		for (int32 Index = 0; Index < DestColors.Num(); ++Index)
		{
			TestTrue(TEXT("R replaced with the LOD0 value"),
				FMath::IsNearlyEqual(DestColors[Index].X, 1.0f, 1e-4f));
			TestTrue(TEXT("G preserved"), FMath::IsNearlyEqual(DestColors[Index].Y, Original[Index].Y));
			TestTrue(TEXT("B preserved"), FMath::IsNearlyEqual(DestColors[Index].Z, Original[Index].Z));
			TestTrue(TEXT("A preserved"), FMath::IsNearlyEqual(DestColors[Index].W, Original[Index].W));
		}
	}

	// --- Paint all channels --------------------------------------------------------------------------------
	{
		TArray<FVector4f> DestColors = {
			FVector4f(0.9f, 0.9f, 0.9f, 0.9f),
			FVector4f(0.8f, 0.8f, 0.8f, 0.8f),
			FVector4f(0.7f, 0.7f, 0.7f, 0.7f),
		};
		Corr::TransferColorChannels(Projection, Colors, Corr::EColorChannelBits::All, DestColors);
		for (const FVector4f& C : DestColors)
		{
			TestTrue(TEXT("All channels take the full LOD0 result"),
				C.Equals(FVector4f(1.0f, 0.0f, 0.0f, 0.0f), 1e-4f));
		}
	}

	// --- A non-projected entry is left exactly as supplied ------------------------------------------------
	{
		Corr::FMeshSurfaceProjectionResult Partial = Projection;
		Partial.Projections[1].bProjected = false;

		TArray<FVector4f> DestColors = {
			FVector4f(0.0f, 0.0f, 0.0f, 1.0f),
			FVector4f(0.5f, 0.6f, 0.7f, 0.8f),
			FVector4f(0.0f, 0.0f, 0.0f, 1.0f),
		};
		Corr::TransferColorChannels(Partial, Colors, Corr::EColorChannelBits::All, DestColors);
		TestTrue(TEXT("Non-projected entry untouched"),
			DestColors[1].Equals(FVector4f(0.5f, 0.6f, 0.7f, 0.8f)));
		TestTrue(TEXT("Projected neighbour still transferred"),
			DestColors[0].Equals(FVector4f(1.0f, 0.0f, 0.0f, 0.0f), 1e-4f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterLODGradientConsistencyTest,
	"PCGUtils.Painter.LODTransfer.GradientConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterLODGradientConsistencyTest::RunTest(const FString&)
{
	// A finely tessellated painted LOD0 with a smooth R = x/100 gradient.
	FDynamicMesh3 Source;
	Source.EnableAttributes();
	Source.Attributes()->EnablePrimaryColors();
	FDynamicMeshColorOverlay* Colors = Source.Attributes()->PrimaryColors();
	const int32 N = 16;
	TArray<int32> VIDs, EIDs;
	VIDs.SetNum((N + 1) * (N + 1));
	EIDs.SetNum((N + 1) * (N + 1));
	for (int32 Y = 0; Y <= N; ++Y)
	{
		for (int32 X = 0; X <= N; ++X)
		{
			const FVector3d P(100.0 * X / N, 100.0 * Y / N, 0.0);
			VIDs[Y * (N + 1) + X] = Source.AppendVertex(P);
			EIDs[Y * (N + 1) + X] = Colors->AppendElement(FVector4f((float)(P.X / 100.0), 0.0f, 0.0f, 1.0f));
		}
	}
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 A = Y * (N + 1) + X, B = Y * (N + 1) + X + 1;
			const int32 C = (Y + 1) * (N + 1) + X + 1, D = (Y + 1) * (N + 1) + X;
			const int32 T0 = Source.AppendTriangle(VIDs[A], VIDs[B], VIDs[C]);
			const int32 T1 = Source.AppendTriangle(VIDs[A], VIDs[C], VIDs[D]);
			Colors->SetTriangle(T0, FIndex3i(EIDs[A], EIDs[B], EIDs[C]));
			Colors->SetTriangle(T1, FIndex3i(EIDs[A], EIDs[C], EIDs[D]));
		}
	}
	FDynamicMeshAABBTree3 Tree(&Source, true);

	// A much coarser "lower LOD" sampling — transferred values should still follow x/100 within interpolation error.
	TArray<FVector3d> Coarse;
	for (int32 I = 0; I <= 5; ++I)
	{
		Coarse.Add(FVector3d(100.0 * I / 5, 40.0, 0.0));
	}
	const Corr::FMeshSurfaceProjectionResult Projection = Corr::ProjectPoints(Tree, Coarse);
	TArray<FVector4f> Dest;
	Dest.Init(FVector4f(0, 0, 0, 1), Coarse.Num());
	Corr::TransferColorChannels(Projection, *Colors, Corr::EColorChannelBits::R, Dest);

	for (int32 Index = 0; Index < Coarse.Num(); ++Index)
	{
		TestTrue(TEXT("Transferred R matches the LOD0 gradient at that position"),
			FMath::IsNearlyEqual(Dest[Index].X, (float)(Coarse[Index].X / 100.0), 1e-3f));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
