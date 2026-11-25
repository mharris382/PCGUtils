// Copyright Max Harris
//
// DynMesh | Planar Cut, Output Selectors: the cut outline (the cross section) and the cap.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Elements/Topology/PCGDynMeshPlanarCut.h"
#include "Factories/PCGUtilsDynMeshRoutedSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPlanarCutSelectorTests
{
	UPCGDynamicMeshData* CutTestBox(const FVector& Center, double Size = 100.0)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform(Center), Size, Size, Size, 2, 2, 2,
			EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	TArray<FPCGTaggedData> RunCut(UPCGDynMeshPlanarCutSettings* Settings, TArray<UPCGDynamicMeshData*> Inputs)
	{
		FPCGDataCollection Input;
		for (UPCGDynamicMeshData* Data : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Data;
			Tagged.Pin = PCGUtilsDynMeshProcessConstants::InputPin;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; }
		}
		return {};
	}

	const UE::Geometry::FDynamicMesh3& CutMesh(const UPCGDynamicMeshData* Data)
	{
		return *Data->GetDynamicMesh()->GetMeshPtr();
	}

	const UPCGDynMeshBooleanSelectionFactoryData* FindCategory(const TArray<FPCGTaggedData>& Selectors, const TCHAR* Tag)
	{
		for (const FPCGTaggedData& Tagged : Selectors)
		{
			if (Tagged.Tags.Contains(Tag)) { return Cast<UPCGDynMeshBooleanSelectionFactoryData>(Tagged.Data); }
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarCutSelectorsTest,
	"PCGUtils.DynMesh.PlanarCut.OutputSelectors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarCutSelectorsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarCutSelectorTests;
	using namespace UE::Geometry;
	const FName SelectorPin = PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
	auto ByPin = [](const TArray<FPCGTaggedData>& Output, FName Pin)
	{ return Output.FilterByPredicate([Pin](const FPCGTaggedData& D) { return D.Pin == Pin; }); };

	// Structural opt-in.
	{
		UPCGNode* Node = NewObject<UPCGNode>();
		UPCGDynMeshPlanarCutSettings* Settings = NewObject<UPCGDynMeshPlanarCutSettings>(Node);
		Node->SetSettingsInterface(Settings);
		TestNull(TEXT("Selectors are off by default"), Node->GetOutputPin(SelectorPin));
		TestTrue(TEXT("Without selectors the cut can still be deferred"), Settings->SupportsDeferredBuilderProcessing());
		Settings->bOutputSelectors = true;
		Node->SetSettingsInterface(Settings);
		const UPCGPin* Pin = Node->GetOutputPin(SelectorPin);
		if (TestNotNull(TEXT("One optional selector output"), Pin))
		{
			TestTrue(TEXT("The pin is a Selector pin"), Pin->GetCurrentTypesID() == FPCGDataTypeIdentifier(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()));
		}
		TestFalse(TEXT("Selector output requires concrete meshes"), Settings->SupportsDeferredBuilderProcessing());
		TestTrue(TEXT("Fill is offered while Fill Holes is on"), Settings->ShowFillSelector());
		Settings->Options.bFillHoles = false;
		TestFalse(TEXT("Fill is hidden when the cut has no cap"), Settings->ShowFillSelector());
	}

	// A plane off the box's own edge loops, so every cut edge is one the cut made.
	const FTransform Plane(FRotator(0.0, 0.0, 0.0), FVector(0.0, 0.0, 17.0));
	auto OutlineLength = [](const UPCGDynMeshBooleanSelectionFactoryData* Selector)
	{
		double Length = 0.0;
		const FDynamicMesh3& Mesh = CutMesh(Selector->ReferenceMesh);
		for (const int32 EID : Selector->EdgeIDs)
		{
			const FIndex2i V = Mesh.GetEdgeV(EID);
			Length += FVector3d::Distance(Mesh.GetVertex(V.A), Mesh.GetVertex(V.B));
		}
		return Length;
	};

	for (const bool bFillHoles : { true, false })
	{
		UPCGDynMeshPlanarCutSettings* Settings = NewObject<UPCGDynMeshPlanarCutSettings>();
		Settings->bOutputSelectors = true;
		Settings->PlaneTransform = Plane;
		Settings->Options.bFillHoles = bFillHoles;
		const TArray<FPCGTaggedData> Output = RunCut(Settings, { CutTestBox(FVector::ZeroVector) });
		const TArray<FPCGTaggedData> Meshes = ByPin(Output, PCGUtilsDynMeshProcessConstants::OutputPin);
		const TArray<FPCGTaggedData> Selectors = ByPin(Output, SelectorPin);
		if (!TestEqual(TEXT("One result mesh"), Meshes.Num(), 1)
			|| !TestEqual(TEXT("Cut, plus Fill only when the cut is capped"), Selectors.Num(), bFillHoles ? 2 : 1))
		{
			return false;
		}
		const UPCGDynamicMeshData* Result = CastChecked<UPCGDynamicMeshData>(Meshes[0].Data);
		const FDynamicMesh3& Mesh = CutMesh(Result);

		const UPCGDynMeshBooleanSelectionFactoryData* Cut = FindCategory(Selectors, TEXT("Cut"));
		if (!TestNotNull(TEXT("The Cut selector carries its tag"), Cut))
		{
			return false;
		}
		TestTrue(TEXT("The Cut selector describes the output mesh"), Cut->ReferenceMesh == Result);
		// The cross section of a 100 box is a 100 square.
		TestEqual(TEXT("The cut edges are the whole outline of the cross section"), OutlineLength(Cut), 400.0, 0.01);
		for (const int32 VID : Cut->VertexIDs)
		{
			const FVector3d P = Mesh.GetVertex(VID);
			TestTrue(TEXT("Cut vertices are on the plane and on the box's sides"),
				FMath::IsNearlyEqual(P.Z, 17.0, 1.e-4) && FMath::IsNearlyEqual(FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)), 50.0, 1.e-4));
		}
		for (const int32 EID : Cut->EdgeIDs)
		{
			TestEqual(TEXT("A cut edge is open exactly when nothing caps it"), Mesh.IsBoundaryEdge(EID), !bFillHoles);
		}

		// The selector works as a Selector: evaluated in the Edge domain it gives the outline.
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = EGeometryElementType::Edge;
		FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Result, Mesh, Domain);
		FGeometrySelection Selection;
		TestTrue(TEXT("The Cut selector evaluates in the Edge domain"), PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Cut, Evaluation, nullptr, Selection));
		// A geometry selection lists an edge once per triangle beside it, so count the distinct edges.
		TSet<int32> EvaluatedEdges;
		for (const uint64 Encoded : Selection.Selection) { const FMeshTriEdgeID TriEdge(FGeoSelectionID(Encoded).GeometryID); EvaluatedEdges.Add(Mesh.GetTriEdge(TriEdge.TriangleID, TriEdge.TriEdgeIndex)); }
		TestTrue(TEXT("Edge evaluation gives exactly the cut edges"), EvaluatedEdges.Num() == Cut->EdgeIDs.Num() && EvaluatedEdges.Includes(Cut->EdgeIDs));

		if (bFillHoles)
		{
			const UPCGDynMeshBooleanSelectionFactoryData* Fill = FindCategory(Selectors, TEXT("Fill"));
			if (TestNotNull(TEXT("The Fill selector carries its tag"), Fill))
			{
				double Area = 0.0;
				for (const int32 TID : Fill->TriangleIDs) { Area += Mesh.GetTriArea(TID); }
				TestEqual(TEXT("The fill faces are the whole cap"), Area, 10000.0, 0.01);
				TestTrue(TEXT("The cap's outline is the cut"), Fill->EdgeIDs.Includes(Cut->EdgeIDs));
			}
		}
	}

	// A plane that misses the mesh cuts nothing: selectors are still output, and are empty.
	{
		UPCGDynMeshPlanarCutSettings* Settings = NewObject<UPCGDynMeshPlanarCutSettings>();
		Settings->bOutputSelectors = true;
		Settings->PlaneTransform = FTransform(FVector(0.0, 0.0, 500.0));
		const TArray<FPCGTaggedData> Selectors = ByPin(RunCut(Settings, { CutTestBox(FVector::ZeroVector) }), SelectorPin);
		TestEqual(TEXT("A miss still outputs both categories"), Selectors.Num(), 2);
		for (const FPCGTaggedData& Tagged : Selectors)
		{
			const UPCGDynMeshBooleanSelectionFactoryData* Selector = Cast<UPCGDynMeshBooleanSelectionFactoryData>(Tagged.Data);
			TestTrue(TEXT("A miss selects nothing"), Selector && Selector->EdgeIDs.IsEmpty() && Selector->TriangleIDs.IsEmpty());
		}
	}

	// Several meshes: still one datum per category, routed to each result.
	{
		UPCGDynMeshPlanarCutSettings* Settings = NewObject<UPCGDynMeshPlanarCutSettings>();
		Settings->bOutputSelectors = true;
		Settings->PlaneTransform = Plane;
		const TArray<FPCGTaggedData> Output = RunCut(Settings, { CutTestBox(FVector::ZeroVector), CutTestBox(FVector(300.0, 0.0, 0.0)) });
		const TArray<FPCGTaggedData> Selectors = ByPin(Output, SelectorPin);
		TestEqual(TEXT("Two result meshes"), ByPin(Output, PCGUtilsDynMeshProcessConstants::OutputPin).Num(), 2);
		if (TestEqual(TEXT("Two meshes share one selector per category"), Selectors.Num(), 2))
		{
			for (const FPCGTaggedData& Tagged : Selectors)
			{
				const UPCGUtilsDynMeshRoutedSelectionFactoryData* Routed = Cast<UPCGUtilsDynMeshRoutedSelectionFactoryData>(Tagged.Data);
				TestTrue(TEXT("Each category routes to both meshes"), Routed && Routed->Routes.Num() == 2);
			}
		}
	}
	return true;
}

#endif
