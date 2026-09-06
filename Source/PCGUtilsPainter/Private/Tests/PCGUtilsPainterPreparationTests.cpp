// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Elements/Painters/PCGDynMeshCombinePainters.h"
#include "Elements/Painters/PCGDynMeshRandomValueByIslandPainter.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

namespace
{
	using namespace UE::Geometry;

	TArray<int32> PrepTestQuad(FDynamicMesh3& Mesh, const FVector3d& Origin, double Size = 10.0)
	{
		const int32 V0 = Mesh.AppendVertex(Origin);
		const int32 V1 = Mesh.AppendVertex(Origin + FVector3d(Size, 0, 0));
		const int32 V2 = Mesh.AppendVertex(Origin + FVector3d(Size, Size, 0));
		const int32 V3 = Mesh.AppendVertex(Origin + FVector3d(0, Size, 0));
		Mesh.AppendTriangle(V0, V1, V2);
		Mesh.AppendTriangle(V0, V2, V3);
		return {V0, V1, V2, V3};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterPreparationLifecycleTest,
	"PCGUtils.Painter.Preparation.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterPreparationLifecycleTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	const TArray<int32> IslandA = PrepTestQuad(Mesh, FVector3d(0, 0, 0));
	const TArray<int32> IslandB = PrepTestQuad(Mesh, FVector3d(1000, 0, 0));
	const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);

	// 1. An existing stateless Painter still works with the new default no-op Prepare().
	{
		UPCGDynMeshAxisGradientPainterFactoryData* Gradient =
			NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
		Gradient->Axis = FVector::UpVector;
		Gradient->StartDistance = 0.0f;
		Gradient->EndDistance = 100.0f;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Gradient->CreateOperation(nullptr);
		TestTrue(TEXT("Stateless Painter initializes"), Op && Op->Initialize(Context));
		TestTrue(TEXT("Stateless Painter's default Prepare is a no-op success"), Op->Prepare(Context));
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.LocalPosition = FVector(0, 0, 50);
		Sample.WorldPosition = Sample.LocalPosition;
		TestTrue(TEXT("Stateless Painter still evaluates"),
			FMath::IsNearlyEqual(Op->Evaluate(Sample).Scalar, 0.5f));
	}

	// 2. A composite Painter prepares its children: Combine(R=Island, G=Island) needs both children's
	//    per-island tables built, so R and G both vary between the two islands.
	{
		UPCGDynMeshRandomValueByIslandPainterFactoryData* IslandR =
			NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		IslandR->Seed = 10;
		UPCGDynMeshRandomValueByIslandPainterFactoryData* IslandG =
			NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		IslandG->Seed = 20;

		UPCGDynMeshCombinePaintersFactoryData* Combine = NewObject<UPCGDynMeshCombinePaintersFactoryData>();
		Combine->ChannelPainters.SetNumZeroed(4);
		Combine->ChannelPainters[0] = IslandR;
		Combine->ChannelPainters[1] = IslandG;

		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Combine->CreateOperation(nullptr);
		TestTrue(TEXT("Composite initializes"), Op && Op->Initialize(Context));
		TestTrue(TEXT("Composite Prepare propagates to children"), Op->Prepare(Context));

		FPCGUtilsDynMeshPainterSample SampleA; SampleA.VertexID = IslandA[0];
		FPCGUtilsDynMeshPainterSample SampleB; SampleB.VertexID = IslandB[0];
		const FVector4f ColorA = Op->Evaluate(SampleA).Color;
		const FVector4f ColorB = Op->Evaluate(SampleB).Color;
		TestNotEqual(TEXT("Child R painter was prepared (varies per island)"), ColorA.X, ColorB.X);
		TestNotEqual(TEXT("Child G painter was prepared (varies per island)"), ColorA.Y, ColorB.Y);
	}

	// 3. Preparation is deterministic and evaluation does not mutate prepared state.
	{
		UPCGDynMeshRandomValueByIslandPainterFactoryData* Island =
			NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		Island->Seed = 42;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Island->CreateOperation(nullptr);
		Op->Initialize(Context);
		Op->Prepare(Context);

		FPCGUtilsDynMeshPainterSample Sample; Sample.VertexID = IslandA[0];
		const float FirstRead = Op->Evaluate(Sample).Scalar;
		for (int32 i = 0; i < 32; ++i)
		{
			TestEqual(TEXT("Repeated Evaluate is stable"), Op->Evaluate(Sample).Scalar, FirstRead);
		}

		// Re-preparing a fresh operation with the same inputs reproduces the value.
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op2 = Island->CreateOperation(nullptr);
		Op2->Initialize(Context);
		Op2->Prepare(Context);
		TestEqual(TEXT("Re-prepared operation matches"), Op2->Evaluate(Sample).Scalar, FirstRead);
	}

	// 4. After Prepare, Evaluate is safe for concurrent calls.
	{
		UPCGDynMeshRandomValueByIslandPainterFactoryData* Island =
			NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		Island->Seed = 7;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Island->CreateOperation(nullptr);
		Op->Initialize(Context);
		Op->Prepare(Context);

		const float Expected = [&]
		{
			FPCGUtilsDynMeshPainterSample S; S.VertexID = IslandB[0];
			return Op->Evaluate(S).Scalar;
		}();

		TArray<float> Results;
		Results.SetNumZeroed(512);
		ParallelFor(Results.Num(), [&](int32 Index)
		{
			FPCGUtilsDynMeshPainterSample S; S.VertexID = IslandB[Index % IslandB.Num()];
			Results[Index] = Op->Evaluate(S).Scalar;
		});
		bool bAllConsistent = true;
		for (const float Value : Results) { bAllConsistent &= FMath::IsNearlyEqual(Value, Expected); }
		TestTrue(TEXT("Concurrent Evaluate returns consistent values"), bAllConsistent);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
