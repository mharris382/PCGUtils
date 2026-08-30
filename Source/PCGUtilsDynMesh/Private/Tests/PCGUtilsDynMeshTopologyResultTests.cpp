// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshPolygroupSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshSelectionFactoryGroup.h"
#include "Elements/Topology/PCGBevelEdges.h"
#include "Elements/Topology/PCGDynMeshExtrude.h"
#include "Elements/Topology/PCGDynMeshInset.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshTopologyResultTests
{
	UPCGPrimitiveBuilderFactoryData* BoxBuilder()
	{
		auto* Box = NewObject<UPCGCreatePrimitiveBoxSettings>();
		Box->DimensionX = Box->DimensionY = Box->DimensionZ = 100;
		Box->Origin = EGeometryScriptPrimitiveOriginMode::Center;
		auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>();
		Builder->Primitive = Box;
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
		return Builder;
	}

	FPCGUtilsDynMeshBuildResult Build(const UPCGData* Data, FVector Offset = FVector::ZeroVector)
	{
		FPCGUtilsDynMeshBuildResult Result;
		const auto* Builder = Cast<UPCGUtilsDynMeshBuilderFactoryData>(Data);
		if (!Builder) { return Result; }
		auto Operation = Builder->CreateOperation(nullptr);
		FPCGUtilsDynMeshBuildContext Context;
		Context.SeedTransform = FTransform(Offset);
		if (Operation && Operation->Build(Context, Result)) { return Result; }
		return {};
	}

	FPCGDataCollection Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, TArray<const UPCGData*> Inputs,
		const UPCGUtilsDynMeshSelectionFactoryData* Selector = nullptr, FName InputPin = TEXT("In"))
	{
		FPCGDataCollection Input;
		for (const UPCGData* Data : Inputs)
		{
			auto& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Data;
			Tagged.Pin = InputPin;
			Tagged.Tags.Add(TEXT("Source"));
		}
		if (Selector)
		{
			auto& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Selector;
			Tagged.Pin = TEXT("Selector");
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData; }
		}
		return {};
	}

	const UPCGData* Main(const FPCGDataCollection& Output, int32 Index = 0)
	{
		const auto Values = Output.GetInputsByPin(TEXT("Out"));
		return Values.IsValidIndex(Index) ? Values[Index].Data.Get() : nullptr;
	}

	const UPCGDynMeshPolygroupSelectionFactoryData* Selector(const FPCGDataCollection& Output)
	{
		const auto Values = Output.GetInputsByPin(TEXT("Result Selector"));
		return Values.Num() == 1 ? Cast<UPCGDynMeshPolygroupSelectionFactoryData>(Values[0].Data) : nullptr;
	}

	const UPCGDynamicMeshData* MeshData(const UPCGData* Data)
	{
		const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(Data);
		return Selection ? Selection->GetSourceMeshData() : Cast<UPCGDynamicMeshData>(Data);
	}

	const UE::Geometry::FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data) { return *Data->GetDynamicMesh()->GetMeshPtr(); }

	UE::Geometry::FGeometrySelection Evaluate(const UPCGUtilsDynMeshSelectionFactoryData* Selector,
		const UPCGDynamicMeshData* Data)
	{
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = UE::Geometry::EGeometryElementType::Face;
		FPCGUtilsDynMeshSelectionEvaluationContext Context(Data, Mesh(Data), Domain);
		UE::Geometry::FGeometrySelection Result;
		PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Context, nullptr, Result);
		return Result;
	}

	bool IsCapAtHeight(const UPCGDynamicMeshData* Data, const UE::Geometry::FGeometrySelection& Selection, double Height)
	{
		if (Selection.Selection.Num() != 2) { return false; }
		for (uint64 EncodedID : Selection.Selection)
		{
			const int32 ID = UE::Geometry::FGeoSelectionID(EncodedID).GeometryID;
			if (!Mesh(Data).IsTriangle(ID) || !FMath::IsNearlyEqual(Mesh(Data).GetTriCentroid(ID).Z, Height, 0.001)) { return false; }
		}
		return true;
	}

	bool SameGeometry(const UPCGDynamicMeshData* A, const UPCGDynamicMeshData* B)
	{
		if (Mesh(A).VertexCount() != Mesh(B).VertexCount() || Mesh(A).TriangleCount() != Mesh(B).TriangleCount()) { return false; }
		for (int32 ID : Mesh(A).VertexIndicesItr())
		{
			if (!Mesh(B).IsVertex(ID) || !Mesh(A).GetVertex(ID).Equals(Mesh(B).GetVertex(ID), 0.001)) { return false; }
		}
		for (int32 ID : Mesh(A).TriangleIndicesItr())
		{
			if (!Mesh(B).IsTriangle(ID) || Mesh(A).GetTriangle(ID) != Mesh(B).GetTriangle(ID) ||
				Mesh(A).GetTriangleGroup(ID) != Mesh(B).GetTriangleGroup(ID)) { return false; }
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshExtrudeResultTest,
	"PCGUtils.DynMesh.TopologyResult.ExtrudeRegionsAndOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshExtrudeResultTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshTopologyResultTests;
	auto Source = Build(BoxBuilder());
	if (!TestTrue(TEXT("Primitive builds"), Source.IsValid())) { return false; }
	auto* Up = NewObject<UPCGDynMeshNormalSelectionFactoryData>();
	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>();
	Settings->Options.Distance = 20;
	Settings->bOutputSelectionData = true;
	Settings->bOutputResultSelector = true; // forces group assignment without bAssignResultPolygroup
	Settings->ResultPolygroupName = TEXT("ExtrudedFace");
	const auto Output = Run(Settings, { Source.MeshData }, Up);
	const auto* SelectionData = Cast<UPCGDynamicMeshSelectionData>(Main(Output));
	if (!TestNotNull(TEXT("Immediate output is result Selection data"), SelectionData) ||
		!TestNotNull(TEXT("Result Selector emitted"), Selector(Output))) { return false; }
	TestTrue(TEXT("Cap is the modified face at the new height"), IsCapAtHeight(SelectionData->GetSourceMeshData(), SelectionData->GetSelection(), 70));
	TestTrue(TEXT("Result is bound to an owned mesh"), SelectionData->GetSourceMeshData() != Source.MeshData);
	TestEqual(TEXT("Upstream topology is unchanged"), Mesh(Source.MeshData).TriangleCount(), 12);
	TestFalse(TEXT("Upstream has no result layer"), Mesh(Source.MeshData).HasAttributes() && Mesh(Source.MeshData).Attributes()->NumPolygroupLayers() != 0);
	TestTrue(TEXT("Named Selector matches the cap"), IsCapAtHeight(SelectionData->GetSourceMeshData(), Evaluate(Selector(Output), SelectionData->GetSourceMeshData()), 70));
	auto* Combined = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
	Combined->ChildFactories = { Selector(Output), Up };
	TestTrue(TEXT("Result Selector composes with an existing normal Selector"),
		IsCapAtHeight(SelectionData->GetSourceMeshData(), Evaluate(Combined, SelectionData->GetSourceMeshData()), 70));
	FPCGUtilsDynMeshSelectionDomain VertexDomain;
	VertexDomain.ElementType = UE::Geometry::EGeometryElementType::Vertex;
	FPCGUtilsDynMeshSelectionEvaluationContext VertexContext(SelectionData->GetSourceMeshData(), Mesh(SelectionData->GetSourceMeshData()), VertexDomain);
	UE::Geometry::FGeometrySelection Vertices;
	TestTrue(TEXT("Result Selector adapts to the vertex domain"),
		PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector(Output), VertexContext, nullptr, Vertices));
	TestEqual(TEXT("Cap Selector resolves the four cap vertices"), Vertices.Selection.Num(), 4);
	TestTrue(TEXT("Tags retained on primary output"), Output.GetInputsByPin(TEXT("Out"))[0].Tags.Contains(TEXT("Source")));
	TSet<int32> ResultGroups;
	for (uint64 ID : SelectionData->GetSelection().Selection)
	{
		ResultGroups.Add(Mesh(SelectionData->GetSourceMeshData()).GetTriangleGroup(UE::Geometry::FGeoSelectionID(ID).GeometryID));
	}
	TestEqual(TEXT("Result assigned to one fresh default PolyGroup"), ResultGroups.Num(), 1);
	for (int32 ID : Mesh(Source.MeshData).TriangleIndicesItr())
	{
		TestFalse(TEXT("Result PolyGroup cannot collide with source groups"), ResultGroups.Contains(Mesh(Source.MeshData).GetTriangleGroup(ID)));
	}
	Settings->ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Border;
	const auto BorderOutput = Run(Settings, { Source.MeshData }, Up);
	const auto* Border = Cast<UPCGDynamicMeshSelectionData>(Main(BorderOutput));
	if (!TestNotNull(TEXT("Border selection produced"), Border)) { return false; }
	TestEqual(TEXT("Side-only mode selects the eight new wall triangles"), Border->GetSelection().Selection.Num(), 8);
	Settings->ResultRegion = EPCGUtilsDynMeshFaceRegionResult::FacesAndBorder;
	const auto AllOutput = Run(Settings, { Source.MeshData }, Up);
	const auto* All = Cast<UPCGDynamicMeshSelectionData>(Main(AllOutput));
	if (!TestNotNull(TEXT("Combined selection produced"), All)) { return false; }
	TestEqual(TEXT("Cap and border mode selects both regions"), All->GetSelection().Selection.Num(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshTopologyChainTest,
	"PCGUtils.DynMesh.TopologyResult.ExtrudeInsetExtrudeImmediateAndBuilder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshTopologyChainTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshTopologyResultTests;
	auto* Leaf = BoxBuilder();
	auto Source = Build(Leaf);
	auto* Up = NewObject<UPCGDynMeshNormalSelectionFactoryData>();
	auto* Extrude = NewObject<UPCGDynMeshExtrudeSettings>();
	Extrude->Options.Distance = 20;
	Extrude->bOutputResultSelector = true;
	Extrude->ResultPolygroupName = TEXT("FirstExtrude");
	auto* Inset = NewObject<UPCGDynMeshInsetSettings>();
	Inset->Options.Distance = 10;
	Inset->bOutputResultSelector = true;
	Inset->ResultPolygroupName = TEXT("InsetFace");
	auto* Last = NewObject<UPCGDynMeshExtrudeSettings>();
	Last->Options.Distance = 30;
	Last->bOutputResultSelector = true;
	Last->ResultPolygroupName = TEXT("LastExtrude");
	const auto Immediate1 = Run(Extrude, { Source.MeshData }, Up);
	const auto Immediate2 = Run(Inset, { Main(Immediate1) }, Selector(Immediate1));
	const auto Immediate3 = Run(Last, { Main(Immediate2) }, Selector(Immediate2));
	const auto* FinalMesh = MeshData(Main(Immediate3));
	if (!TestNotNull(TEXT("Immediate chain produces a mesh"), FinalMesh)) { return false; }
	TestTrue(TEXT("Final cap reaches expected height"), IsCapAtHeight(FinalMesh, Evaluate(Selector(Immediate3), FinalMesh), 100));
	TestTrue(TEXT("Earlier named result still resolves after another group is assigned"),
		IsCapAtHeight(FinalMesh, Evaluate(Selector(Immediate2), FinalMesh), 100));
	for (uint64 EncodedID : Evaluate(Selector(Immediate3), FinalMesh).Selection)
	{
		const auto Triangle = Mesh(FinalMesh).GetTriangle(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const auto Position = Mesh(FinalMesh).GetVertex(Triangle[Corner]);
			TestTrue(TEXT("Inset shrank the final face"), FMath::Abs(Position.X) <= 40.001 && FMath::Abs(Position.Y) <= 40.001);
		}
	}
	const auto Deferred1 = Run(Extrude, { Leaf }, Up);
	const auto Deferred2 = Run(Inset, { Main(Deferred1) }, Selector(Deferred1));
	// No explicit Selector needed here: the Builder carries Inset's replacement selection internally.
	const auto Deferred3 = Run(Last, { Main(Deferred2) });
	if (!TestNotNull(TEXT("Deferred node emits a Selector before building any geometry"), Selector(Deferred3))) { return false; }
	Extrude->Options.Distance = Inset->Options.Distance = Last->Options.Distance = 999;
	Last->ResultPolygroupName = TEXT("ChangedAfterCapture");
	const auto Built = Build(Main(Deferred3));
	if (!TestTrue(TEXT("Deferred chain builds from captured settings"), Built.IsValid())) { return false; }
	TestTrue(TEXT("Immediate and deferred results match"), SameGeometry(FinalMesh, Built.MeshData));
	TestTrue(TEXT("Builder result carries a cap selection"), Built.HasSelection() && IsCapAtHeight(Built.MeshData, Built.SelectionData->GetSelection(), 100));
	TestTrue(TEXT("Prebuilt Selector resolves on realized geometry"), IsCapAtHeight(Built.MeshData, Evaluate(Selector(Deferred3), Built.MeshData), 100));
	const auto Shifted = Build(Main(Deferred3), FVector(200, 30, 40));
	if (!TestTrue(TEXT("Same expression works for another seed"), Shifted.IsValid())) { return false; }
	TestTrue(TEXT("Named Selector resolves independently of seed position"), IsCapAtHeight(Shifted.MeshData, Evaluate(Selector(Deferred3), Shifted.MeshData), 140));
	TestTrue(TEXT("Seed results own distinct meshes"), Shifted.MeshData != Built.MeshData);
	TestTrue(TEXT("Builder frame retained"), Shifted.bHasBuilderFrame && Shifted.BuilderFrame.GetLocation().Equals(FVector(200, 30, 40)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshTopologyEmptyTest,
	"PCGUtils.DynMesh.TopologyResult.MissingAndEmptySelections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshTopologyEmptyTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshTopologyResultTests;
	using namespace UE::Geometry;
	auto* Leaf = BoxBuilder();
	auto Source = Build(Leaf);
	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>();
	Settings->bOutputResultSelector = true;
	Settings->bOutputSelectionData = true;
	AddExpectedError(TEXT("requires either DynMesh Selection data or a connected Selector"), EAutomationExpectedErrorFlags::Contains, 2);
	TestNull(TEXT("Missing required selection skips immediate input"), Main(Run(Settings, { Source.MeshData })));
	const auto Deferred = Run(Settings, { Leaf });
	TestFalse(TEXT("Missing requirement is checked when Builder is materialized"), Build(Main(Deferred)).IsValid());
	FGeometrySelection Empty;
	Empty.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
	auto* EmptyData = NewObject<UPCGDynamicMeshSelectionData>();
	EmptyData->Initialize(Source.MeshData, Empty);
	const auto Output = Run(Settings, { EmptyData });
	const auto* Result = Cast<UPCGDynamicMeshSelectionData>(Main(Output));
	if (!TestNotNull(TEXT("Explicit empty selection remains valid input"), Result)) { return false; }
	TestEqual(TEXT("Empty input never extrudes the whole mesh"), Mesh(Result->GetSourceMeshData()).TriangleCount(), 12);
	TestEqual(TEXT("Empty result stays empty in immediate output"), Result->GetSelection().Selection.Num(), 0);
	TestEqual(TEXT("Empty named region selects nothing"), Evaluate(Selector(Output), Result->GetSourceMeshData()).Selection.Num(), 0);
	TestEqual(TEXT("Result Selector on an unrelated mesh selects nothing"), Evaluate(Selector(Output), Source.MeshData).Selection.Num(), 0);
	const auto EmptyDeferred = Run(Settings, { Leaf }, Selector(Output));
	const auto Built = Build(Main(EmptyDeferred));
	TestTrue(TEXT("Empty Selector is valid for deferred evaluation"), Built.IsValid() && Built.HasSelection());
	if (Built.IsValid())
	{
		TestEqual(TEXT("Deferred empty selection does no geometry work"), Mesh(Built.MeshData).TriangleCount(), 12);
		TestEqual(TEXT("Deferred output selection is empty"), Built.SelectionData->GetSelection().Selection.Num(), 0);
	}
	Settings->bRequireSelection = false;
	TestNotNull(TEXT("Disabling the requirement allows a bare mesh"), Main(Run(Settings, { Source.MeshData })));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshTopologyIdentityTest,
	"PCGUtils.DynMesh.TopologyResult.NamesPinsAndMultipleMeshes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshTopologyIdentityTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshTopologyResultTests;
	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>();
	auto* Other = NewObject<UPCGDynMeshExtrudeSettings>();
	TestTrue(TEXT("Default region names are distinct per node"), Settings->GetResultPolygroupName() != Other->GetResultPolygroupName());
	auto* Override = DuplicateObject<UPCGDynMeshExtrudeSettings>(Settings, GetTransientPackage());
	Override->OriginalSettings = Settings;
	TestEqual(TEXT("PCG overrides keep the authoring node's region identity"), Override->GetResultPolygroupName(), Settings->GetResultPolygroupName());
	const auto PinProperties = Settings->AllInputPinProperties();
	const auto* MainPin = PinProperties.FindByPredicate([](const FPCGPinProperties& P) { return P.Label == TEXT("In"); });
	TestTrue(TEXT("Primary input accepts selection, mesh, and Builder"), MainPin &&
		MainPin->AllowedTypes.Intersects(FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh)) &&
		MainPin->AllowedTypes.Intersects(FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass())) &&
		MainPin->AllowedTypes.Intersects(FPCGDataTypeIdentifier(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId())));
	auto* Node = NewObject<UPCGNode>();
	auto* NodeSettings = NewObject<UPCGDynMeshExtrudeSettings>(Node);
	Node->SetSettingsInterface(NodeSettings);
	auto* Anchor = NewObject<UPCGNode>();
	Anchor->SetSettingsInterface(Anchor->GetSettings());
	const FPCGDataTypeIdentifier BuilderType(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	Anchor->GetOutputPin(TEXT("Out"))->Properties.AllowedTypes = BuilderType;
	Node->GetInputPin(TEXT("In"))->AddEdgeTo(Anchor->GetOutputPin(TEXT("Out")));
	TestTrue(TEXT("Builder input narrows primary output to Builder"), Node->GetOutputPin(TEXT("Out"))->GetCurrentTypesID() == BuilderType);
	TestTrue(TEXT("Companion pin stays Selector when primary output becomes Builder"),
		Node->GetOutputPin(TEXT("Result Selector"))->GetCurrentTypesID() == FPCGDataTypeIdentifier(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()));
	Settings->bOutputResultSelector = true;
	Settings->Options.Distance = 20;
	auto A = Build(BoxBuilder());
	auto B = Build(BoxBuilder());
	B.MeshData->GetMutableDynamicMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& Edit)
	{
		for (int32 ID : Edit.TriangleIndicesItr()) { Edit.SetTriangleGroup(ID, Edit.GetTriangleGroup(ID) + 1000); }
	});
	const auto Output = Run(Settings, { A.MeshData, B.MeshData }, NewObject<UPCGDynMeshNormalSelectionFactoryData>());
	TestEqual(TEXT("One mesh-independent Selector for all primary results"), Output.GetInputsByPin(TEXT("Result Selector")).Num(), 1);
	TestEqual(TEXT("Both primary meshes emitted"), Output.GetInputsByPin(TEXT("Out")).Num(), 2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const auto* Result = MeshData(Main(Output, Index));
		if (!TestNotNull(TEXT("Result mesh exists"), Result)) { return false; }
		TestTrue(TEXT("Selector works despite different numeric group allocations"), IsCapAtHeight(Result, Evaluate(Selector(Output), Result), 70));
	}
	const auto Deferred = Run(Settings, { BoxBuilder() }, NewObject<UPCGDynMeshNormalSelectionFactoryData>());
	const auto* FirstBuilder = Cast<UPCGUtilsDynMeshProcessBuilderFactoryData>(Main(Deferred));
	Settings->ResultPolygroupName = TEXT("DifferentResult");
	const auto Changed = Run(Settings, { BoxBuilder() }, NewObject<UPCGDynMeshNormalSelectionFactoryData>());
	const auto* SecondBuilder = Cast<UPCGUtilsDynMeshProcessBuilderFactoryData>(Main(Changed));
	TestTrue(TEXT("Result name participates in operation cache identity"), FirstBuilder && SecondBuilder && FirstBuilder->OperationConfigCrc != SecondBuilder->OperationConfigCrc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBevelResultTest,
	"PCGUtils.DynMesh.TopologyResult.BevelFaces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBevelResultTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshTopologyResultTests;
	auto Source = Build(BoxBuilder());
	auto* Settings = NewObject<UPCGBevelEdgesSettings>();
	Settings->bOutputSelectionData = Settings->bOutputResultSelector = true;
	const auto Output = Run(Settings, { Source.MeshData }, nullptr, TEXT("Mesh"));
	const auto* Result = Cast<UPCGDynamicMeshSelectionData>(Main(Output));
	if (!TestNotNull(TEXT("Bevel emits result Selection data"), Result)) { return false; }
	TestTrue(TEXT("Bevel reports new face topology"), Result->GetSelection().Selection.Num() > 0);
	const auto Selected = Evaluate(Selector(Output), Result->GetSourceMeshData());
	TestEqual(TEXT("Named Selector isolates the bevel faces"), Selected.Selection.Num(), Result->GetSelection().Selection.Num());
	TestTrue(TEXT("Bevel result domain is faces, not stale input edges"), Result->GetSelection().ElementType == UE::Geometry::EGeometryElementType::Face);
	const auto Deferred = Run(Settings, { BoxBuilder() }, nullptr, TEXT("Mesh"));
	const auto Built = Build(Main(Deferred));
	if (!TestTrue(TEXT("Deferred bevel builds"), Built.IsValid())) { return false; }
	TestTrue(TEXT("Deferred bevel matches immediate"), SameGeometry(Result->GetSourceMeshData(), Built.MeshData));
	TestTrue(TEXT("Builder retains the new bevel selection"), Built.HasSelection() && Built.SelectionData->GetSelection().Selection.Num() == Selected.Selection.Num());
	return true;
}

#endif
