// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Painters/PCGDynMeshRandomValueByIslandPainter.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Helpers/PCGHelpers.h"
#include "Math/RandomStream.h"

namespace
{
	using namespace UE::Geometry;

	/** Appends one axis-aligned quad (2 triangles) at Origin in the z=Origin.Z plane. Returns its 4 vertex IDs. */
	TArray<int32> IslandTestQuad(FDynamicMesh3& Mesh, const FVector3d& Origin, double Size = 10.0)
	{
		const int32 V0 = Mesh.AppendVertex(Origin);
		const int32 V1 = Mesh.AppendVertex(Origin + FVector3d(Size, 0, 0));
		const int32 V2 = Mesh.AppendVertex(Origin + FVector3d(Size, Size, 0));
		const int32 V3 = Mesh.AppendVertex(Origin + FVector3d(0, Size, 0));
		Mesh.AppendTriangle(V0, V1, V2);
		Mesh.AppendTriangle(V0, V2, V3);
		return {V0, V1, V2, V3};
	}

	FPCGUtilsDynMeshPainterValue EvaluateAt(
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation>& Operation, int32 VertexID)
	{
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.VertexID = VertexID;
		return Operation->Evaluate(Sample);
	}

	TSharedPtr<FPCGUtilsDynMeshPainterOperation> MakeIslandOperation(
		FDynamicMesh3& Mesh, int32 Seed, float MinValue, float MaxValue,
		const FPCGUtilsDynMeshPainterEvaluationContext& Context)
	{
		UPCGDynMeshRandomValueByIslandPainterFactoryData* Factory =
			NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		Factory->Seed = Seed;
		Factory->MinValue = MinValue;
		Factory->MaxValue = MaxValue;

		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Factory->CreateOperation(nullptr);
		if (!Operation || !Operation->Initialize(Context) || !Operation->Prepare(Context))
		{
			return nullptr;
		}
		return Operation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterMeshIslandTest,
	"PCGUtils.Painter.MeshIsland.Values",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterMeshIslandTest::RunTest(const FString&)
{
	// One connected mesh -> one value for every vertex.
	{
		FDynamicMesh3 Mesh;
		const TArray<int32> Quad = IslandTestQuad(Mesh, FVector3d::ZeroVector);
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = MakeIslandOperation(Mesh, 1234, 0.0f, 1.0f, Context);
		TestNotNull(TEXT("Island operation prepared"), Op.Get());
		const float First = EvaluateAt(Op, Quad[0]).Scalar;
		for (const int32 VID : Quad)
		{
			TestEqual(TEXT("Every vertex of one island shares its value"), EvaluateAt(Op, VID).Scalar, First);
		}
		TestTrue(TEXT("Value within range"), First >= 0.0f && First <= 1.0f);
	}

	// Two disconnected pieces -> distinct per-piece values, uniform within each piece.
	{
		FDynamicMesh3 Mesh;
		const TArray<int32> A = IslandTestQuad(Mesh, FVector3d(0, 0, 0));
		const TArray<int32> B = IslandTestQuad(Mesh, FVector3d(1000, 0, 0));
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = MakeIslandOperation(Mesh, 77, -5.0f, 5.0f, Context);
		TestNotNull(TEXT("Two-island operation prepared"), Op.Get());

		const float ValueA = EvaluateAt(Op, A[0]).Scalar;
		const float ValueB = EvaluateAt(Op, B[0]).Scalar;
		for (const int32 VID : A) { TestEqual(TEXT("Island A uniform"), EvaluateAt(Op, VID).Scalar, ValueA); }
		for (const int32 VID : B) { TestEqual(TEXT("Island B uniform"), EvaluateAt(Op, VID).Scalar, ValueB); }
		TestNotEqual(TEXT("Two islands get different values"), ValueA, ValueB);
		TestTrue(TEXT("Island A in range"), ValueA >= -5.0f && ValueA <= 5.0f);
		TestTrue(TEXT("Island B in range"), ValueB >= -5.0f && ValueB <= 5.0f);
	}

	// Determinism: same seed + topology -> identical values; different seed -> (very likely) different.
	{
		FDynamicMesh3 Mesh;
		const TArray<int32> A = IslandTestQuad(Mesh, FVector3d(0, 0, 0));
		IslandTestQuad(Mesh, FVector3d(1000, 0, 0));
		IslandTestQuad(Mesh, FVector3d(2000, 0, 0));
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);

		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op1 = MakeIslandOperation(Mesh, 999, 0.0f, 1.0f, Context);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op1b = MakeIslandOperation(Mesh, 999, 0.0f, 1.0f, Context);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op2 = MakeIslandOperation(Mesh, 111, 0.0f, 1.0f, Context);
		TestEqual(TEXT("Same seed reproduces the value"),
			EvaluateAt(Op1, A[0]).Scalar, EvaluateAt(Op1b, A[0]).Scalar);
		TestNotEqual(TEXT("A different seed changes the value"),
			EvaluateAt(Op1, A[0]).Scalar, EvaluateAt(Op2, A[0]).Scalar);
	}

	// Seams do not split an island: a shared-position UV/color seam kept in an overlay stays one component.
	{
		FDynamicMesh3 Mesh;
		const int32 V0 = Mesh.AppendVertex(FVector3d(0, 0, 0));
		const int32 V1 = Mesh.AppendVertex(FVector3d(10, 0, 0));
		const int32 V2 = Mesh.AppendVertex(FVector3d(10, 10, 0));
		const int32 V3 = Mesh.AppendVertex(FVector3d(0, 10, 0));
		const int32 T0 = Mesh.AppendTriangle(V0, V1, V2);
		const int32 T1 = Mesh.AppendTriangle(V0, V2, V3);
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnablePrimaryColors();
		FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
		// A hard colour seam across the shared V0-V2 edge: split elements, same base topology.
		Colors->SetTriangle(T0, FIndex3i(
			Colors->AppendElement(FVector4f(1, 0, 0, 1)), Colors->AppendElement(FVector4f(1, 0, 0, 1)),
			Colors->AppendElement(FVector4f(1, 0, 0, 1))));
		Colors->SetTriangle(T1, FIndex3i(
			Colors->AppendElement(FVector4f(0, 1, 0, 1)), Colors->AppendElement(FVector4f(0, 1, 0, 1)),
			Colors->AppendElement(FVector4f(0, 1, 0, 1))));

		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = MakeIslandOperation(Mesh, 5, 0.0f, 1.0f, Context);
		const float Value = EvaluateAt(Op, V0).Scalar;
		for (const int32 VID : {V0, V1, V2, V3})
		{
			TestEqual(TEXT("Colour seam does not create a second island"), EvaluateAt(Op, VID).Scalar, Value);
		}
	}

	// Empty mesh and invalid sample IDs are safe.
	{
		FDynamicMesh3 Empty;
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Empty, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = MakeIslandOperation(Empty, 0, 2.0f, 3.0f, Context);
		TestNotNull(TEXT("Empty mesh still prepares"), Op.Get());
		TestEqual(TEXT("Invalid vertex ID returns the floor value"), EvaluateAt(Op, 999).Scalar, 2.0f);
		TestEqual(TEXT("INDEX_NONE sample returns the floor value"), EvaluateAt(Op, INDEX_NONE).Scalar, 2.0f);
	}

	// Sparse vertex IDs + an isolated vertex do not cause out-of-bounds access.
	{
		FDynamicMesh3 Sparse;
		IslandTestQuad(Sparse, FVector3d(0, 0, 0));           // triangles 0,1 ; vertices 0..3
		const TArray<int32> B = IslandTestQuad(Sparse, FVector3d(1000, 0, 0)); // triangles 2,3 ; vertices 4..7
		const int32 IsolatedVID = Sparse.AppendVertex(FVector3d(500, 500, 0)); // vertex 8, no triangles
		// Punch a hole in island B's ID range: drop its two triangles, then one now-unused vertex.
		Sparse.RemoveTriangle(2);
		Sparse.RemoveTriangle(3);
		Sparse.RemoveVertex(B[2]);

		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Sparse, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = MakeIslandOperation(Sparse, 3, 0.0f, 1.0f, Context);
		TestNotNull(TEXT("Sparse-ID mesh prepares"), Op.Get());

		// The edge-isolated vertex is its own single-vertex island keyed by its own ID — a real per-island
		// draw, not a silent floor value.
		const float IsolatedValue = EvaluateAt(Op, IsolatedVID).Scalar;
		const float ExpectedIsolated = FRandomStream(PCGHelpers::ComputeSeed(3, IsolatedVID)).FRandRange(0.0f, 1.0f);
		TestTrue(TEXT("Isolated vertex gets its own deterministic island value"),
			FMath::IsNearlyEqual(IsolatedValue, ExpectedIsolated, 1e-5f));

		for (int32 VID = 0; VID < Sparse.MaxVertexID() + 4; ++VID)
		{
			const float Value = EvaluateAt(Op, VID).Scalar;
			TestTrue(TEXT("Every sampled value stays in range"), Value >= 0.0f && Value <= 1.0f);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
