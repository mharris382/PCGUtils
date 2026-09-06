// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Elements/Painters/PCGDynMeshSelectionPainterSwitch.h"
#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshSelectionFactoryGroup.h"
#include "Elements/Selections/PCGSelectDynamicMeshTriangles.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

namespace
{
	using namespace UE::Geometry;

	/** Flat quad (tri normal +Z, verts 0..3) and a wall quad (tri normal +X, verts 4..7), disconnected. */
	void BuildTwoFacingQuads(FDynamicMesh3& Mesh, TArray<int32>& OutFlat, TArray<int32>& OutWall)
	{
		Mesh = FDynamicMesh3();
		const int32 F0 = Mesh.AppendVertex(FVector3d(0, 0, 0));
		const int32 F1 = Mesh.AppendVertex(FVector3d(10, 0, 0));
		const int32 F2 = Mesh.AppendVertex(FVector3d(10, 10, 0));
		const int32 F3 = Mesh.AppendVertex(FVector3d(0, 10, 0));
		Mesh.AppendTriangle(F0, F1, F2);
		Mesh.AppendTriangle(F0, F2, F3);
		OutFlat = {F0, F1, F2, F3};

		const int32 W0 = Mesh.AppendVertex(FVector3d(1000, 0, 0));
		const int32 W1 = Mesh.AppendVertex(FVector3d(1000, 10, 0));
		const int32 W2 = Mesh.AppendVertex(FVector3d(1000, 10, 10));
		const int32 W3 = Mesh.AppendVertex(FVector3d(1000, 0, 10));
		Mesh.AppendTriangle(W0, W1, W2);
		Mesh.AppendTriangle(W0, W2, W3);
		OutWall = {W0, W1, W2, W3};

		// Baked per-vertex normals (flat -> +Z, wall -> +X) so a vertex-domain normal Selector has real data
		// without an attribute overlay.
		Mesh.EnableVertexNormals(FVector3f::UnitZ());
		for (const int32 VID : OutFlat) { Mesh.SetVertexNormal(VID, FVector3f(0, 0, 1)); }
		for (const int32 VID : OutWall) { Mesh.SetVertexNormal(VID, FVector3f(1, 0, 0)); }
	}

	UPCGDynamicMeshData* MakeMeshData(const FDynamicMesh3& Mesh)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(FDynamicMesh3(Mesh));
		return Data;
	}

	float EvalScalar(const TSharedPtr<FPCGUtilsDynMeshPainterOperation>& Op, int32 VertexID)
	{
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.VertexID = VertexID;
		// A local-space z for the Axis-Gradient painter branch: use the vertex index so branches are distinguishable.
		Sample.LocalPosition = FVector(0, 0, 50);
		Sample.WorldPosition = Sample.LocalPosition;
		return Op->Evaluate(Sample).Scalar;
	}

	UPCGDynMeshSelectionPainterSwitchFactoryData* MakeSwitch(
		const UPCGUtilsDynMeshSelectionFactoryData* Selector,
		EPCGUtilsPainterBranchSource SelSource, float SelConst, const UPCGUtilsDynMeshPainterFactoryData* SelPainter,
		EPCGUtilsPainterBranchSource UnselSource, float UnselConst, const UPCGUtilsDynMeshPainterFactoryData* UnselPainter)
	{
		UPCGDynMeshSelectionPainterSwitchFactoryData* Factory =
			NewObject<UPCGDynMeshSelectionPainterSwitchFactoryData>();
		Factory->Selector = Selector;
		Factory->SelectedSource = SelSource;
		Factory->SelectedConstant = SelConst;
		Factory->SelectedPainter = SelPainter;
		Factory->UnselectedSource = UnselSource;
		Factory->UnselectedConstant = UnselConst;
		Factory->UnselectedPainter = UnselPainter;
		return Factory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterSelectionSwitchTest,
	"PCGUtils.Painter.SelectionSwitch.Branches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterSelectionSwitchTest::RunTest(const FString&)
{
	FDynamicMesh3 Mesh;
	TArray<int32> Flat, Wall;
	BuildTwoFacingQuads(Mesh, Flat, Wall);
	UPCGDynamicMeshData* MeshData = MakeMeshData(Mesh);
	const FPCGUtilsDynMeshPainterEvaluationContext Context(
		MeshData, Mesh, FTransform::Identity, 0, 1, /*bIsNativeDynMeshTarget=*/true);

	// A Face-native Selector (triangle normal ~ +Z): selects the flat quad, converts to its 4 vertices.
	UPCGSelectDynamicMeshTrianglesFactoryData* FaceSelector =
		NewObject<UPCGSelectDynamicMeshTrianglesFactoryData>();
	FaceSelector->Mode = EPCGDynamicMeshTriangleSelectionMode::FaceNormal;
	FaceSelector->ReferenceNormal = FVector::UpVector;
	FaceSelector->MinimumDotProduct = 0.5;

	auto PrepSwitch = [&Context](UPCGDynMeshSelectionPainterSwitchFactoryData* Factory)
		-> TSharedPtr<FPCGUtilsDynMeshPainterOperation>
	{
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Factory->CreateOperation(nullptr);
		if (!Op || !Op->Initialize(Context) || !Op->Prepare(Context)) { return nullptr; }
		return Op;
	};

	// --- Constant / Constant --------------------------------------------------------------------------------
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			FaceSelector,
			EPCGUtilsPainterBranchSource::Constant, 1.0f, nullptr,
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr));
		TestNotNull(TEXT("Constant/Constant switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestEqual(TEXT("Selected vertex -> Selected constant"), EvalScalar(Op, VID), 1.0f); }
		for (const int32 VID : Wall) { TestEqual(TEXT("Unselected vertex -> Unselected constant"), EvalScalar(Op, VID), 0.0f); }
		// Only the active branch is used: results are exactly 1 or 0, never an average.
		for (const int32 VID : Wall) { TestNotEqual(TEXT("No evaluate-both-and-lerp"), EvalScalar(Op, VID), 0.5f); }
	}

	// --- Painter / Constant --------------------------------------------------------------------------------
	UPCGDynMeshAxisGradientPainterFactoryData* Gradient =
		NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
	Gradient->Axis = FVector::UpVector;
	Gradient->StartDistance = 0.0f;
	Gradient->EndDistance = 100.0f; // z=50 -> 0.5
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			FaceSelector,
			EPCGUtilsPainterBranchSource::Painter, 0.0f, Gradient,
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr));
		TestNotNull(TEXT("Painter/Constant switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestTrue(TEXT("Selected -> gradient painter (0.5)"), FMath::IsNearlyEqual(EvalScalar(Op, VID), 0.5f)); }
		for (const int32 VID : Wall) { TestEqual(TEXT("Unselected -> constant 0"), EvalScalar(Op, VID), 0.0f); }
	}

	// --- Constant / Painter --------------------------------------------------------------------------------
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			FaceSelector,
			EPCGUtilsPainterBranchSource::Constant, 1.0f, nullptr,
			EPCGUtilsPainterBranchSource::Painter, 0.0f, Gradient));
		TestNotNull(TEXT("Constant/Painter switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestEqual(TEXT("Selected -> constant 1"), EvalScalar(Op, VID), 1.0f); }
		for (const int32 VID : Wall) { TestTrue(TEXT("Unselected -> gradient painter (0.5)"), FMath::IsNearlyEqual(EvalScalar(Op, VID), 0.5f)); }
	}

	// --- Painter / Painter (two different constants via gradient start/end) -------------------------------
	{
		UPCGDynMeshAxisGradientPainterFactoryData* GradientHigh =
			NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
		GradientHigh->Axis = FVector::UpVector;
		GradientHigh->StartDistance = 0.0f;
		GradientHigh->EndDistance = 50.0f; // z=50 -> clamps to 1.0
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			FaceSelector,
			EPCGUtilsPainterBranchSource::Painter, 0.0f, GradientHigh,
			EPCGUtilsPainterBranchSource::Painter, 0.0f, Gradient));
		TestNotNull(TEXT("Painter/Painter switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestTrue(TEXT("Selected -> high gradient (1.0)"), FMath::IsNearlyEqual(EvalScalar(Op, VID), 1.0f)); }
		for (const int32 VID : Wall) { TestTrue(TEXT("Unselected -> mid gradient (0.5)"), FMath::IsNearlyEqual(EvalScalar(Op, VID), 0.5f)); }
	}

	// --- A vertex-native Selector converts to the vertex domain -----------------------------------------
	{
		UPCGDynMeshNormalSelectionFactoryData* VertexSelector =
			NewObject<UPCGDynMeshNormalSelectionFactoryData>();
		VertexSelector->ReferenceDirection = FVector::UpVector;
		VertexSelector->DotThreshold = 0.5f;

		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			VertexSelector,
			EPCGUtilsPainterBranchSource::Constant, 1.0f, nullptr,
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr));
		TestNotNull(TEXT("Vertex-native Selector switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestEqual(TEXT("Flat verts (+Z normal) selected"), EvalScalar(Op, VID), 1.0f); }
		for (const int32 VID : Wall) { TestEqual(TEXT("Wall verts (+X normal) not selected"), EvalScalar(Op, VID), 0.0f); }
	}

	// --- A composite Selector (AND of two Face-native triangle predicates) works ------------------------
	{
		UPCGSelectDynamicMeshTrianglesFactoryData* FaceSelector2 =
			NewObject<UPCGSelectDynamicMeshTrianglesFactoryData>();
		FaceSelector2->Mode = EPCGDynamicMeshTriangleSelectionMode::FaceNormal;
		FaceSelector2->ReferenceNormal = FVector::UpVector;
		FaceSelector2->MinimumDotProduct = 0.0; // permissive: both selectors must agree for AND to select

		UPCGDynMeshSelectionFactoryGroupData* AndGroup = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
		AndGroup->Mode = EPCGUtilsDynMeshSelectionFactoryGroupMode::And;
		AndGroup->ChildFactories = {FaceSelector, FaceSelector2};

		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = PrepSwitch(MakeSwitch(
			AndGroup,
			EPCGUtilsPainterBranchSource::Constant, 1.0f, nullptr,
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr));
		TestNotNull(TEXT("Composite (AND) Selector switch prepared"), Op.Get());
		for (const int32 VID : Flat) { TestEqual(TEXT("Flat verts pass both predicates"), EvalScalar(Op, VID), 1.0f); }
		for (const int32 VID : Wall) { TestEqual(TEXT("Wall verts fail the AND"), EvalScalar(Op, VID), 0.0f); }
	}

	// --- Missing required Painter input fails safely ---------------------------------------------------
	{
		UPCGDynMeshSelectionPainterSwitchFactoryData* Bad = MakeSwitch(
			FaceSelector,
			EPCGUtilsPainterBranchSource::Painter, 0.0f, nullptr,   // Painter source, no Painter
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr);
		AddExpectedError(TEXT("branch is set to Painter but no Painter is connected"),
			EAutomationExpectedErrorFlags::Contains, 0);
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Bad->CreateOperation(nullptr);
		TestFalse(TEXT("Switch with a missing branch Painter fails to initialize"),
			Op.IsValid() && Op->Initialize(Context));
	}

	// --- Missing Selector fails safely --------------------------------------------------------------------
	{
		UPCGDynMeshSelectionPainterSwitchFactoryData* NoSelector = MakeSwitch(
			nullptr,
			EPCGUtilsPainterBranchSource::Constant, 1.0f, nullptr,
			EPCGUtilsPainterBranchSource::Constant, 0.0f, nullptr);
		AddExpectedError(TEXT("requires a Selector"), EAutomationExpectedErrorFlags::Contains, 0);
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = NoSelector->CreateOperation(nullptr);
		TestFalse(TEXT("Switch with no Selector fails to initialize"),
			Op.IsValid() && Op->Initialize(Context));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
