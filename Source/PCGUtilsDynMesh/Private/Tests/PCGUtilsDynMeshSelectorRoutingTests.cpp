// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Elements/Selections/PCGBuildDynMeshSelection.h"
#include "Elements/Topology/PCGBevelEdges.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Factories/PCGUtilsDynMeshRoutedSelectionFactory.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshSelectorRoutingTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	UPCGDynamicMeshData* Box(FVector Center = FVector::ZeroVector, double Size = 100)
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
			FTransform(Center), Size, Size, Size, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}
	const FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data) { return *Data->GetDynamicMesh()->GetMeshPtr(); }
	FPCGTaggedData Tagged(const UPCGData* Data, FName Pin)
	{ FPCGTaggedData D; D.Data = Data; D.Pin = Pin; return D; }
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<FPCGTaggedData> Pins)
	{
		FPCGDataCollection Input; Input.TaggedData = MoveTemp(Pins); Input.TaggedData.Emplace_GetRef().Data = Settings;
		const auto Element = Settings->GetElement(); TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true; Context->AsyncState.NumAvailableTasks = 1;
		for (int32 I = 0; I < 20; ++I) { if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; } }
		return {};
	}
	TArray<FPCGTaggedData> OnPin(const TArray<FPCGTaggedData>& Outputs, FName Pin)
	{ return Outputs.FilterByPredicate([Pin](const auto& D) { return D.Pin == Pin; }); }
	FGeometrySelection Evaluate(const UPCGUtilsDynMeshSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data)
	{
		FPCGUtilsDynMeshSelectionDomain Domain; Domain.ElementType = EGeometryElementType::Vertex;
		FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh(Data), Domain); FGeometrySelection Result;
		PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Evaluation, nullptr, Result); return Result;
	}
	bool SameMembership(const FGeometrySelection& A, const FGeometrySelection& B)
	{ return A.Selection.Num() == B.Selection.Num() && A.Selection.Intersect(B.Selection).Num() == A.Selection.Num(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectorIdentityRoutingTest, "PCGUtils.DynMesh.SelectorRouting.IdentityAndCopies", PCGUtilsDynMeshSelectorRoutingTests::Flags)
bool FPCGDynMeshSelectorIdentityRoutingTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectorRoutingTests;
	auto* Parent = Box();
	auto* A = CastChecked<UPCGDynamicMeshData>(Parent->DuplicateData(nullptr));
	auto* B = CastChecked<UPCGDynamicMeshData>(Parent->DuplicateData(nullptr));
	auto* Unrelated = Box();
	auto* SA = NewObject<UPCGDynMeshBooleanSelectionFactoryData>(); SA->ReferenceMesh = A; SA->VertexIDs.Add(0);
	auto* SB = NewObject<UPCGDynMeshBooleanSelectionFactoryData>(); SB->ReferenceMesh = B; SB->VertexIDs.Add(1); SB->VertexIDs.Add(2);
	auto* Routed = NewObject<UPCGUtilsDynMeshRoutedSelectionFactoryData>();
	Routed->Routes.Add({A, SA}); Routed->Routes.Add({B, SB});
	TestEqual(TEXT("Identical first mesh uses only its own membership"), Evaluate(Routed, A).Selection.Num(), 1);
	TestEqual(TEXT("Identical second mesh uses only its own membership"), Evaluate(Routed, B).Selection.Num(), 2);
	TestTrue(TEXT("Unrelated identical geometry is an empty match"), Evaluate(Routed, Unrelated).IsEmpty());
	TestTrue(TEXT("Even a single mesh-bound selector rejects unrelated identical geometry"), Evaluate(SA, B).IsEmpty());
	const auto* Copy = CastChecked<UPCGDynamicMeshData>(A->DuplicateData(nullptr));
	auto* ProcessCopy = NewObject<UPCGDynamicMeshData>(); ProcessCopy->InitializeFromData(Copy); ProcessCopy->Initialize(FDynamicMesh3(Mesh(Copy)));
	TestEqual(TEXT("Native duplication and process copies preserve the route"), Evaluate(Routed, ProcessCopy).Selection.Num(), 1);
	Routed->Routes.Swap(0, 1);
	TestEqual(TEXT("Routing is independent of child order"), Evaluate(Routed, ProcessCopy).Selection.Num(), 1);
	auto* OtherRoute = NewObject<UPCGUtilsDynMeshRoutedSelectionFactoryData>(); OtherRoute->Routes.Add({B, SA});
	auto* FirstRoute = NewObject<UPCGUtilsDynMeshRoutedSelectionFactoryData>(); FirstRoute->Routes.Add({A, SA});
	TestNotEqual(TEXT("Routing identity participates in cache keys"), OtherRoute->GetOrComputeCrc(true).GetValue(), FirstRoute->GetOrComputeCrc(true).GetValue());
	ProcessCopy->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& M) { M.SetVertex(0, M.GetVertex(0) + FVector3d(1,0,0)); });
	TestTrue(TEXT("Changed geometry does not apply stale Boolean membership"), Evaluate(Routed, ProcessCopy).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectorBatchPipelineTest, "PCGUtils.DynMesh.SelectorRouting.BooleanBevelBatch", PCGUtilsDynMeshSelectorRoutingTests::Flags)
bool FPCGDynMeshSelectorBatchPipelineTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectorRoutingTests;
	auto* Boolean = NewObject<UPCGDynMeshBooleanSettings>(); Boolean->bOutputSelectors = true;
	Boolean->Mode = EPCGBooleanOperationMode::EachAWithEveryB;
	Boolean->BooleanOperation = EGeometryScriptBooleanOperation::Subtract;
	// Two overlapping pairs plus two disjoint combinations produce four outputs, including empty rims.
	const auto Output = Run(Boolean, {Tagged(Box(), TEXT("InA")), Tagged(Box(FVector(300,0,0)), TEXT("InA")),
		Tagged(Box(FVector(40,15,10),80), TEXT("InB")), Tagged(Box(FVector(340,15,10),80), TEXT("InB"))});
	auto Meshes = OnPin(Output, TEXT("Out")); const auto Selectors = OnPin(Output, TEXT("Selector"));
	if (!TestEqual(TEXT("Four result meshes"), Meshes.Num(), 4) || !TestEqual(TEXT("One selector datum per category regardless of result count"), Selectors.Num(), 2)) { return false; }
	const auto* RimDatum = Selectors.FindByPredicate([](const auto& D) { return D.Tags.Contains(TEXT("Overlapping")); });
	if (!TestNotNull(TEXT("The bundled rim retains its category tag"), RimDatum)) { return false; }
	const auto* Rim = Cast<UPCGUtilsDynMeshRoutedSelectionFactoryData>(RimDatum->Data);
	if (!TestNotNull(TEXT("Multi-mesh result uses a switchboard selector"), Rim)) { return false; }
	TestEqual(TEXT("The switchboard has a branch for every result, including empty regions"), Rim->Routes.Num(), 4);
	for (const auto& Route : Rim->Routes)
	{
		TestTrue(TEXT("Bundled membership equals the correct individual result"), SameMembership(Evaluate(Rim, Route.Mesh), Evaluate(Route.Selector, Route.Mesh)));
	}
	// Feed the entire batch through the existing single-datum Selector contract, deliberately reordered.
	Meshes.Swap(0, 3);
	TArray<FPCGTaggedData> BevelInputs;
	for (auto D : Meshes) { D.Pin = TEXT("Mesh"); BevelInputs.Add(D); }
	BevelInputs.Add(Tagged(Rim, TEXT("Selector")));
	auto* Bevel = NewObject<UPCGBevelEdgesSettings>(); Bevel->BevelDistance.Constant = 0.5; Bevel->bOutputSelectionData = true; Bevel->bOutputResultSelector = true;
	const auto Beveled = Run(Bevel, MoveTemp(BevelInputs)); const auto Results = OnPin(Beveled, TEXT("Out"));
	if (!TestEqual(TEXT("One selector handles all four Bevel inputs"), Results.Num(), 4)) { return false; }
	for (int32 I = 0; I < Results.Num(); ++I)
	{
		const auto* Original = CastChecked<UPCGDynamicMeshData>(Meshes[I].Data);
		const auto* Result = CastChecked<UPCGDynamicMeshSelectionData>(Results[I].Data);
		TestEqual(TEXT("Empty rims stay empty and cut rims generate bevel faces"), Result->GetSelection().IsEmpty(), Evaluate(Rim, Original).IsEmpty());
	}
	const auto BevelSelectors = OnPin(Beveled, TEXT("Result"));
	if (!TestEqual(TEXT("Bevel still emits one reusable selector for the batch"), BevelSelectors.Num(), 1)) { return false; }
	auto* Build = NewObject<UPCGBuildDynMeshSelectionSettings>(); TArray<FPCGTaggedData> BuildInputs;
	for (const auto& D : Results) { const auto* S = CastChecked<UPCGDynamicMeshSelectionData>(D.Data); BuildInputs.Add(Tagged(S->GetSourceMeshData(), TEXT("Mesh"))); }
	BuildInputs.Add(Tagged(BevelSelectors[0].Data, TEXT("Selectors")));
	const auto Built = OnPin(Run(Build, MoveTemp(BuildInputs)), TEXT("Selection"));
	if (!TestEqual(TEXT("Bevel result selector materializes across the batch"), Built.Num(), 4)) { return false; }
	for (int32 I = 0; I < Built.Num(); ++I)
	{
		TestTrue(TEXT("Each materialized Bevel result matches its own generated faces"), SameMembership(
			CastChecked<UPCGDynamicMeshSelectionData>(Built[I].Data)->GetSelection(), CastChecked<UPCGDynamicMeshSelectionData>(Results[I].Data)->GetSelection()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectorMissingOperandBatchTest, "PCGUtils.DynMesh.SelectorRouting.EmptyBatchRegions", PCGUtilsDynMeshSelectorRoutingTests::Flags)
bool FPCGDynMeshSelectorMissingOperandBatchTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectorRoutingTests;
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>(); Settings->bOutputSelectors = true;
	const auto Output = Run(Settings, {Tagged(Box(), TEXT("InA")), Tagged(Box(FVector(300,0,0)), TEXT("InA"))});
	const auto Selectors = OnPin(Output, TEXT("Selector")); TestEqual(TEXT("Missing operand still emits one datum per category"), Selectors.Num(), 2);
	for (const auto& S : Selectors)
	{
		const auto* Routed = Cast<UPCGUtilsDynMeshRoutedSelectionFactoryData>(S.Data);
		if (!TestNotNull(TEXT("No-op batch uses the same routing contract"), Routed)) { return false; }
		for (const auto& Route : Routed->Routes) { TestTrue(TEXT("No-op routes are explicitly empty"), Evaluate(Routed, Route.Mesh).IsEmpty()); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectorRetainedSnapshotTest, "PCGUtils.DynMesh.SelectorRouting.RetainedSequentialSnapshot", PCGUtilsDynMeshSelectorRoutingTests::Flags)
bool FPCGDynMeshSelectorRetainedSnapshotTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectorRoutingTests;
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>(); Settings->bOutputSelectors = true;
	Settings->Mode = EPCGBooleanOperationMode::EachAWithEachBSequentially;
	Settings->BooleanOperation = EGeometryScriptBooleanOperation::Subtract;
	Settings->BooleanOperationOptions.bAllowEmptyResult = false;
	for (bool bBatch : { false, true })
	{
		TArray<FPCGTaggedData> Inputs = { Tagged(Box(), TEXT("InA")), Tagged(Box(FVector(40,15,10),80), TEXT("InB")), Tagged(Box(FVector::ZeroVector,1000), TEXT("InB")) };
		if (bBatch) { Inputs.Add(Tagged(Box(FVector(300,0,0)), TEXT("InA"))); }
		const auto Output = Run(Settings, MoveTemp(Inputs));
		const auto Results = OnPin(Output, TEXT("Out")); const auto Selectors = OnPin(Output, TEXT("Selector"));
		if (!TestEqual(TEXT("Rejected later subtraction preserves outputs"), Results.Num(), bBatch ? 2 : 1)) { return false; }
		const auto* RimDatum = Selectors.FindByPredicate([](const auto& D) { return D.Tags.Contains(TEXT("Overlapping")); });
		if (!TestNotNull(TEXT("Retained rim category is emitted"), RimDatum)) { return false; }
		const auto* Selector = CastChecked<UPCGUtilsDynMeshSelectionFactoryData>(RimDatum->Data);
		TestFalse(TEXT("Retained snapshot membership routes to the final output"), Evaluate(Selector, CastChecked<UPCGDynamicMeshData>(Results[0].Data)).IsEmpty());
		if (bBatch) { TestTrue(TEXT("Uncut sibling keeps its empty rim"), Evaluate(Selector, CastChecked<UPCGDynamicMeshData>(Results[1].Data)).IsEmpty()); }
	}
	return true;
}

#endif
