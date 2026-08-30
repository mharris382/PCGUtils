// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Elements/Selections/PCGDynMeshPolygroupSelectionFactory.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPolygroupSelectionTests
{
	UPCGDynamicMeshData* MakeQuad()
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		Mesh.EnableTriangleGroups(0);
		Mesh.AppendVertex(FVector3d(0, 0, 0));
		Mesh.AppendVertex(FVector3d(1, 0, 0));
		Mesh.AppendVertex(FVector3d(1, 1, 0));
		Mesh.AppendVertex(FVector3d(0, 1, 0));
		Mesh.AppendTriangle(0, 1, 2, 3);
		Mesh.AppendTriangle(0, 2, 3, 9);
		auto* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	bool Evaluate(const UPCGDynMeshPolygroupSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data,
		UE::Geometry::EGeometryElementType Domain, TArray<int32>& OutIDs)
	{
		FPCGUtilsDynMeshSelectionDomain SelectionDomain;
		SelectionDomain.ElementType = Domain;
		const auto& Mesh = *Data->GetDynamicMesh()->GetMeshPtr();
		FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh, SelectionDomain);
		UE::Geometry::FGeometrySelection Result;
		OutIDs.Reset();
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Evaluation, nullptr, Result)) { return false; }
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(MoveTemp(Result));
		const EGeometryScriptIndexType IndexType = Domain == UE::Geometry::EGeometryElementType::Face ? EGeometryScriptIndexType::Triangle :
			(Domain == UE::Geometry::EGeometryElementType::Vertex ? EGeometryScriptIndexType::Vertex : EGeometryScriptIndexType::Edge);
		return ScriptSelection.ConvertToMeshIndexArray(Mesh, OutIDs, IndexType) == IndexType;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPolygroupIDsTest,
	"PCGUtils.DynMesh.PolygroupSelector.IDsLayersAndInversion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPolygroupIDsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPolygroupSelectionTests;
	using namespace UE::Geometry;
	auto* Data = MakeQuad();
	auto* Settings = NewObject<UPCGDynMeshPolygroupSelectionFactoryProviderSettings>();
	Settings->GroupIDs = { 3, 3, 999 };
	auto* Selector = CastChecked<UPCGDynMeshPolygroupSelectionFactoryData>(Settings->CreateFactory(nullptr));
	TArray<int32> IDs;
	TestTrue(TEXT("Selector evaluates"), Evaluate(Selector, Data, EGeometryElementType::Face, IDs));
	TestTrue(TEXT("Duplicate and unknown IDs do not change membership"), IDs.Num() == 1 && IDs.Contains(0));
	Selector->bInvertSelection = true;
	Evaluate(Selector, Data, EGeometryElementType::Face, IDs);
	TestTrue(TEXT("Invert selects the other group"), IDs.Num() == 1 && IDs.Contains(1));
	Selector->GroupIDs = { 3, 9 };
	Selector->bInvertSelection = false;
	Evaluate(Selector, Data, EGeometryElementType::Face, IDs);
	TestEqual(TEXT("Multiple IDs form a union"), IDs.Num(), 2);
	Selector->GroupIDs.Reset();
	Evaluate(Selector, Data, EGeometryElementType::Face, IDs);
	TestEqual(TEXT("Empty list selects nothing"), IDs.Num(), 0);
	Selector->bInvertSelection = true;
	Evaluate(Selector, Data, EGeometryElementType::Face, IDs);
	TestEqual(TEXT("Inverted empty list selects all"), IDs.Num(), 2);

	Data->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumPolygroupLayers(1);
		Mesh.Attributes()->GetPolygroupLayer(0)->SetValue(0, 9);
		Mesh.Attributes()->GetPolygroupLayer(0)->SetValue(1, 3);
	});
	Selector->bInvertSelection = false;
	Selector->GroupIDs = { 3 };
	Selector->GroupLayer.bDefaultLayer = false;
	TestTrue(TEXT("Extended layer evaluates"), Evaluate(Selector, Data, EGeometryElementType::Face, IDs));
	TestTrue(TEXT("Reads chosen layer rather than default groups"), IDs.Num() == 1 && IDs.Contains(1));
	TestEqual(TEXT("Default groups unchanged"), Data->GetDynamicMesh()->GetMeshPtr()->GetTriangleGroup(0), 3);
	Selector->GroupLayer.ExtendedLayerIndex = -1;
	AddExpectedError(TEXT("could not find extended PolyGroup layer"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Negative layer rejected safely"), Evaluate(Selector, Data, EGeometryElementType::Face, IDs));
	Selector->GroupLayer.ExtendedLayerIndex = 5;
	TestFalse(TEXT("Missing layer rejected"), Evaluate(Selector, Data, EGeometryElementType::Face, IDs));
	const auto Pins = Settings->AllOutputPinProperties();
	TestTrue(TEXT("Provider emits the existing typed Selector pin"), Pins.Num() == 1 && Pins[0].Label == TEXT("Selector") &&
		Pins[0].AllowedTypes == FPCGDataTypeIdentifier(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPolygroupDomainsTest,
	"PCGUtils.DynMesh.PolygroupSelector.DomainsAndProcessIntersection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPolygroupDomainsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPolygroupSelectionTests;
	using namespace UE::Geometry;
	auto* Data = MakeQuad();
	auto* Selector = NewObject<UPCGDynMeshPolygroupSelectionFactoryData>();
	Selector->GroupIDs = { 3 };
	TArray<int32> IDs;
	TestTrue(TEXT("Converts faces to vertices"), Evaluate(Selector, Data, EGeometryElementType::Vertex, IDs));
	TestTrue(TEXT("Inclusive vertices contain the entire triangle"), IDs.Num() == 3 && IDs.Contains(0) && IDs.Contains(1) && IDs.Contains(2));
	TestTrue(TEXT("Converts faces to edges"), Evaluate(Selector, Data, EGeometryElementType::Edge, IDs));
	TestEqual(TEXT("Inclusive edges contain all triangle edges"), IDs.Num(), 3);
	Selector->bAllowPartialInclusion = false;
	Evaluate(Selector, Data, EGeometryElementType::Vertex, IDs);
	TestTrue(TEXT("Strict conversion excludes shared boundary vertices"), IDs.Num() == 1 && IDs.Contains(1));
	Evaluate(Selector, Data, EGeometryElementType::Edge, IDs);
	TestEqual(TEXT("Strict conversion excludes shared boundary edge"), IDs.Num(), 2);

	FGeometrySelection Incoming;
	Incoming.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
	Incoming.Selection.Add(FGeoSelectionID::MeshTriangle(1).Encoded());
	auto* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
	SelectionData->Initialize(Data, MoveTemp(Incoming));
	const auto Resolved = FPCGUtilsDynMeshProcessFunctions::ResolveInput(
		SelectionData, Selector, FPCGUtilsDynMeshProcessSelectionPolicy(), nullptr);
	TestTrue(TEXT("Selector integrates with process resolver"), Resolved.IsValid() && Resolved.SelectionData);
	if (Resolved.SelectionData)
	{
		TestTrue(TEXT("Incoming disjoint selection is intersected"), Resolved.SelectionData->GetSelection().IsEmpty());
		TestTrue(TEXT("Selection retains original mesh identity"), Resolved.MeshData == Data);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPolygroupHighestTest,
	"PCGUtils.DynMesh.PolygroupSelector.HighestUsedIDAndBoolean", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPolygroupHighestTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPolygroupSelectionTests;
	using namespace UE::Geometry;
	auto* Data = MakeQuad();
	Data->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.SetTriangleGroup(0, 1000);
		Mesh.SetTriangleGroup(0, 3); // Leave the allocation counter above any used group.
	});
	auto* Selector = NewObject<UPCGDynMeshPolygroupSelectionFactoryData>();
	Selector->SelectionMode = EPCGUtilsDynMeshPolygroupSelectionMode::HighestGroupID;
	TArray<int32> IDs;
	TestTrue(TEXT("Highest mode evaluates"), Evaluate(Selector, Data, EGeometryElementType::Face, IDs));
	TestTrue(TEXT("Uses live IDs rather than allocation counter"), IDs.Num() == 1 && IDs.Contains(1));
	auto* Other = MakeQuad();
	Other->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh) { Mesh.SetTriangleGroup(0, 20); });
	Evaluate(Selector, Other, EGeometryElementType::Face, IDs);
	TestTrue(TEXT("Shared Selector recomputes highest ID per mesh"), IDs.Num() == 1 && IDs.Contains(0));

	auto* A = NewObject<UPCGDynamicMeshData>();
	auto* B = NewObject<UPCGDynamicMeshData>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(A->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
		FTransform::Identity, 100, 100, 100, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(B->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
		FTransform(FVector(40, 15, 5)), 80, 80, 80, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	A->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh)
	{
		for (int32 TID : Mesh.TriangleIndicesItr()) { Mesh.SetTriangleGroup(TID, 17); }
	});
	auto* Boolean = NewObject<UPCGDynMeshBooleanSettings>();
	Boolean->BooleanOperation = EGeometryScriptBooleanOperation::Subtract;
	Boolean->bAssignOperandPolygroup = true;
	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.MeshData = A;
	Invocation.OperandMeshData = B;
	FPCGUtilsDynMeshProcessOutcome Outcome;
	TestTrue(TEXT("Boolean creates operand group"), Boolean->CreateProcessOperation(nullptr)->Execute(Invocation, Outcome));
	TestTrue(TEXT("Selector evaluates boolean result"), Evaluate(Selector, A, EGeometryElementType::Face, IDs));
	const auto& Mesh = *A->GetDynamicMesh()->GetMeshPtr();
	int32 ExpectedCount = 0;
	for (int32 TID : Mesh.TriangleIndicesItr())
	{
		if (Mesh.GetTriangleGroup(TID) != 17) { ++ExpectedCount; }
	}
	TestTrue(TEXT("Boolean has operand-derived faces"), ExpectedCount > 0);
	TestEqual(TEXT("Selector isolates operand-derived faces"), IDs.Num(), ExpectedCount);
	for (int32 TID : IDs) { TestTrue(TEXT("No primary faces selected"), Mesh.GetTriangleGroup(TID) > 17); }
	return true;
}

#endif
