// Copyright Max Harris
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_AUTOMATION_TESTS
#include "Tests/PCGUtilsFractureTestHelpers.h"
#include "Elements/PCGGeometryCollectionDataflow.h"
#include "Dataflow/PCGUtilsDataflowNodes.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowGraph.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowVariableNodes.h"
#include "PCGParamData.h"

namespace PCGUtilsDataflowTests
{
	using namespace PCGUtilsFractureTests;
	struct FGraphFixture
	{
		UDataflow* Asset = NewObject<UDataflow>();
		UPCGGeometryCollectionDataflowSettings* Settings = NewObject<UPCGGeometryCollectionDataflowSettings>();
		TSharedPtr<FPCGUtilsDataflowCollectionInputNode> Input;
		TSharedPtr<FPCGUtilsDataflowCollectionOutputNode> Output;
		FGraphFixture()
		{
			Input = MakeShared<FPCGUtilsDataflowCollectionInputNode>(UE::Dataflow::FNodeParameters{TEXT("Input"), Asset});
			Output = MakeShared<FPCGUtilsDataflowCollectionOutputNode>(UE::Dataflow::FNodeParameters{TEXT("Output"), Asset});
			Asset->GetDataflow()->AddNode(Input);
			Asset->GetDataflow()->AddNode(Output);
			Connect(*Input, TEXT("Collection"), *Output, TEXT("Collection"));
			Connect(*Input, TEXT("Materials"), *Output, TEXT("Materials"));
			Settings->DataflowAsset = Asset;
			Settings->bPointsInCollectionSpace = false;
			Settings->RefreshInterface();
		}
		bool Connect(FDataflowNode& From, FName Out, FDataflowNode& To, FName In)
		{
			return Asset->GetDataflow()->Connect(*From.FindOutput(Out), *To.FindInput(In));
		}
	};
	UPCGParamData* Param(float Value)
	{
		auto* Data = NewObject<UPCGParamData>();
		const auto Key = Data->Metadata->AddEntry();
		Data->Metadata->CreateAttribute<float>(TEXT("Chance"), 0, false, false)->SetValue(Key, Value);
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDataflowRoundTrip, "PCGUtils.Fracture.Dataflow.RoundTripAndBatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGUtilsDataflowRoundTrip::RunTest(const FString&)
{
	using namespace PCGUtilsDataflowTests;
	FGraphFixture Graph;
	const auto* Source = ToCollection(Box());
	if (!TestNotNull(TEXT("Source collection"), Source)) return false;
	const int32 SourceBones = Source->GetCollection().Transform.Num();
	const auto Result = Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("GC"), Source}, {TEXT("Parameters"), Param(0.5f)}});
	TestEqual(TEXT("Two GC datasets broadcast one Parameters dataset"), Result.Num(), 2);
	if (Result.Num() == 2)
	{
		const auto* A = Cast<UPCGGeometryCollectionData>(Result[0].Data);
		const auto* B = Cast<UPCGGeometryCollectionData>(Result[1].Data);
		if (!TestNotNull(TEXT("GC A"), A) || !TestNotNull(TEXT("GC B"), B)) return false;
		TestEqual(TEXT("Geometry preserved"), A->GetCollection().Indices.Num(), Source->GetCollection().Indices.Num());
		TestTrue(TEXT("Fresh lineage"), A->GetCollectionId() != Source->GetCollectionId());
		TestTrue(TEXT("Independent batch lineages"), A->GetCollectionId() != B->GetCollectionId());
		TestTrue(TEXT("Fresh bone identities"), PCGUtilsGeometryCollectionIdentity::GetBoneId(A->GetCollection(), 0) != PCGUtilsGeometryCollectionIdentity::GetBoneId(Source->GetCollection(), 0));
		TestEqual(TEXT("Materials preserved"), A->GetMaterials().Num(), Source->GetMaterials().Num());
	}
	TestEqual(TEXT("Source immutable"), Source->GetCollection().Transform.Num(), SourceBones);
	TestFalse(TEXT("No PCG evaluation cache"), Graph.Settings->GetElement()->IsCacheable(Graph.Settings));
	TestEqual(TEXT("No selection pin"), Graph.Settings->InputPinProperties().Num(), 2);
	AddExpectedError(TEXT("Input dataset counts must be N:N or N:1"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Mismatched counts produce no output"), Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("GC"), Source},
		{TEXT("Parameters"), Param(0)}, {TEXT("Parameters"), Param(0)}, {TEXT("Parameters"), Param(0)}}).Num(), 0);
	AddExpectedError(TEXT("Input dataset counts must be N:N or N:1"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Empty required input rejected"), Run(Graph.Settings, {}).Num(), 0);
	const auto Again = Run(Graph.Settings, {{TEXT("GC"), Source}});
	if (Again.Num() == 1 && Result.Num() == 2)
		TestTrue(TEXT("Regeneration does not reuse output state"), CastChecked<UPCGGeometryCollectionData>(Again[0].Data)->GetStateId() != CastChecked<UPCGGeometryCollectionData>(Result[0].Data)->GetStateId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDataflowFracture, "PCGUtils.Fracture.Dataflow.FractureAndParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGUtilsDataflowFracture::RunTest(const FString&)
{
	using namespace PCGUtilsDataflowTests;
	FModuleManager::Get().LoadModule(TEXT("GeometryCollectionNodes"));
	FGraphFixture Graph;
	Graph.Asset->Variables.AddProperty(TEXT("Chance"), EPropertyBagPropertyType::Float);
	Graph.Asset->Variables.SetValueFloat(TEXT("Chance"), 0.f);
	auto Variable = MakeShared<FGetDataflowVariableNode>(UE::Dataflow::FNodeParameters{TEXT("Chance"), Graph.Asset});
	Variable->SetVariable(Graph.Asset, TEXT("Chance"));
	Graph.Asset->GetDataflow()->AddNode(Variable);
	auto Sites = MakeShared<FPCGUtilsDataflowPointsInputNode>(UE::Dataflow::FNodeParameters{TEXT("Sites"), Graph.Asset});
	Graph.Asset->GetDataflow()->AddNode(Sites);
	auto Cutter = UE::Dataflow::FNodeFactory::GetInstance()->NewNodeFromRegisteredType(*Graph.Asset->GetDataflow(),
		{FGuid::NewGuid(), TEXT("FVoronoiFractureDataflowNode_v2"), TEXT("Fracture"), Graph.Asset});
	if (!TestTrue(TEXT("Engine Voronoi node registered"), Cutter.IsValid())) return false;
	TestTrue(TEXT("GC input connection"), Graph.Connect(*Graph.Input, TEXT("Collection"), *Cutter, TEXT("Collection")));
	TestTrue(TEXT("Site array connection"), Graph.Connect(*Sites, TEXT("Positions"), *Cutter, TEXT("Points")));
	TestTrue(TEXT("Variable connection"), Graph.Connect(*Variable, TEXT("Value"), *Cutter, TEXT("ChanceToFracture")));
	Graph.Asset->GetDataflow()->Disconnect(Graph.Input->FindOutput(FName(TEXT("Collection"))), Graph.Output->FindInput(FName(TEXT("Collection"))));
	TestTrue(TEXT("GC output connection"), Graph.Connect(*Cutter, TEXT("Collection"), *Graph.Output, TEXT("Collection")));
	Graph.Settings->RefreshInterface();
	TestEqual(TEXT("Inline variable discovered"), Graph.Settings->Parameters.Num(), 1);
	const auto* Source = ToCollection(Box());
	const auto* Points = SiteGrid(2);
	if (!Source) return false;
	const auto Default = Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("Sites"), Points}});
	TestEqual(TEXT("Default batch output"), Default.Num(), 1);
	if (Default.Num() == 1)
		TestEqual(TEXT("Asset default chance zero leaves geometry intact"), CastChecked<UPCGGeometryCollectionData>(Default[0].Data)->GetCollection().Transform.Num(), Source->GetCollection().Transform.Num());
	Graph.Settings->Parameters[0].bOverride = true;
	Graph.Settings->Parameters[0].RealValue = 1;
	const auto Inline = Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("Sites"), Points}});
	TestEqual(TEXT("Inline output"), Inline.Num(), 1);
	if (Inline.Num() == 1)
		TestTrue(TEXT("Inline chance one fractures"), CastChecked<UPCGGeometryCollectionData>(Inline[0].Data)->GetCollection().Transform.Num() > Source->GetCollection().Transform.Num());
	const auto Batch = Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("Sites"), Points}, {TEXT("Parameters"), Param(0)}, {TEXT("Parameters"), Param(1)}});
	TestEqual(TEXT("One GC and point dataset broadcast over two parameter datasets"), Batch.Num(), 2);
	if (Batch.Num() == 2)
	{
		TestEqual(TEXT("Metadata wins over inline"), CastChecked<UPCGGeometryCollectionData>(Batch[0].Data)->GetCollection().Transform.Num(), Source->GetCollection().Transform.Num());
		TestTrue(TEXT("Second context uses its own variables"), CastChecked<UPCGGeometryCollectionData>(Batch[1].Data)->GetCollection().Transform.Num() > Source->GetCollection().Transform.Num());
	}
	TestEqual(TEXT("Asset default unmodified"), Graph.Asset->Variables.GetValueFloat(TEXT("Chance")).GetValue(), 0.f);
	auto* Bad = NewObject<UPCGParamData>();
	Bad->Metadata->AddEntry();
	Bad->Metadata->CreateAttribute<FString>(TEXT("Chance"), TEXT("invalid"), false, false);
	AddExpectedError(TEXT("expects Float"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Invalid metadata type rejects entire batch"), Run(Graph.Settings, {{TEXT("GC"), Source}, {TEXT("Sites"), Points}, {TEXT("Parameters"), Param(0)}, {TEXT("Parameters"), Bad}}).Num(), 0);
	return true;
}
#endif
