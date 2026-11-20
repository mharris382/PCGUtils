// Copyright Max Harris
#include "Misc/AutomationTest.h"
#if WITH_AUTOMATION_TESTS
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Selections/PCGDynMeshIssuesSelection.h"
#include "Elements/Topology/PCGDynMeshRepairMesh.h"
#include "Geometry/PCGUtilsDynMeshRepair.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshRepairMeshTests
{
	using namespace UE::Geometry;
	using namespace PCGUtilsDynMeshRepair;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	int32 Triangle(FDynamicMesh3& M, FVector3d A, FVector3d B, FVector3d C)
	{
		return M.AppendTriangle(M.AppendVertex(A), M.AppendVertex(B), M.AppendVertex(C));
	}
	FDynamicMesh3 Fan(FVector3d Center = FVector3d(1.5, 0.5, 0), bool bAttributes = false)
	{
		FDynamicMesh3 M; M.EnableTriangleGroups(3);
		M.AppendVertex(FVector3d(0,0,0)); M.AppendVertex(FVector3d(1,0,0)); M.AppendVertex(FVector3d(1,1,0)); M.AppendVertex(FVector3d(0,1,0)); M.AppendVertex(Center);
		for (int32 I = 0; I < 4; ++I) { M.AppendTriangle(I, (I + 1) % 4, 4, 3); }
		if (bAttributes)
		{
			M.EnableAttributes(); M.Attributes()->EnableMaterialID(); M.Attributes()->EnablePrimaryColors();
			M.Attributes()->SetNumPolygroupLayers(1);
			auto* UV = M.Attributes()->PrimaryUV(); auto* Normals = M.Attributes()->PrimaryNormals(); auto* Colors = M.Attributes()->PrimaryColors();
			for (int32 VID : M.VertexIndicesItr())
			{
				const FVector3d P = M.GetVertex(VID);
				UV->AppendElement(FVector2f(P.X, P.Y)); Normals->AppendElement(FVector3f(0,0,1)); Colors->AppendElement(FVector4f(P.X, P.Y, 0, 1));
			}
			for (int32 TID : M.TriangleIndicesItr())
			{
				const FIndex3i V = M.GetTriangle(TID); UV->SetTriangle(TID,V); Normals->SetTriangle(TID,V); Colors->SetTriangle(TID,V);
				M.Attributes()->GetMaterialID()->SetValue(TID, 7); M.Attributes()->GetPolygroupLayer(0)->SetValue(TID, 11);
			}
		}
		return M;
	}
	FGeometrySelection Evaluate(const UPCGUtilsDynMeshSelectionFactoryData* Factory, const UPCGDynamicMeshData* Data, EGeometryElementType Type = EGeometryElementType::Face)
	{
		FPCGUtilsDynMeshSelectionDomain Domain; Domain.ElementType = Type;
		FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, *Data->GetDynamicMesh()->GetMeshPtr(), Domain);
		FGeometrySelection Out; UPCGUtilsDynMeshSelectionFactoryData* Mutable = const_cast<UPCGUtilsDynMeshSelectionFactoryData*>(Factory);
		PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Mutable, Evaluation, nullptr, Out); return Out;
	}
	TArray<FPCGTaggedData> Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, const UPCGData* Input, const UPCGData* Selector = nullptr)
	{
		FPCGDataCollection Collection;
		auto& Main = Collection.TaggedData.Emplace_GetRef(); Main.Pin = TEXT("In"); Main.Data = Input; Main.Tags.Add(TEXT("RepairTest"));
		if (Selector) { auto& S = Collection.TaggedData.Emplace_GetRef(); S.Pin = TEXT("Selector"); S.Data = Selector; }
		Collection.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Collection, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true; Context->AsyncState.NumAvailableTasks = 1;
		for (int32 I = 0; I < 16; ++I) { if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; } }
		return {};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairDetectionTest, "PCGUtils.DynMesh.RepairMesh.Detection", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairDetectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	FChecks Checks; Checks.bDegenerateTriangles = false;
	FDynamicMesh3 Clean = Fan(FVector3d(0.5,0.5,0));
	TestTrue(TEXT("Shared edges and vertices are normal contact"), FindIssues(Clean,Checks).Triangles.IsEmpty());
	FDynamicMesh3 Folded = Fan(); const auto Issues = FindIssues(Folded, Checks);
	TestTrue(TEXT("Folded neighbours produce multiple bad triangles"), Issues.Triangles.Num() > 1);
	TestTrue(TEXT("Every intersecting pair contributes both triangles"), Issues.Intersections.Num() > 0);
	for (const FIntPoint& Pair : Issues.Intersections) { TestTrue(TEXT("Both pair members selected"), Issues.Triangles.Contains(Pair.X) && Issues.Triangles.Contains(Pair.Y)); }
	FDynamicMesh3 Nested;
	Triangle(Nested, FVector3d(0,0,0),FVector3d(10,0,0),FVector3d(0,10,0));
	Triangle(Nested, FVector3d(1,1,0),FVector3d(2,1,0),FVector3d(1,2,0));
	TestEqual(TEXT("Fully contained coplanar overlap"), FindIssues(Nested,Checks).Triangles.Num(), 2);
	FDynamicMesh3 Crossing;
	Triangle(Crossing, FVector3d(-1,-1,0),FVector3d(1,-1,0),FVector3d(0,1,0));
	Triangle(Crossing, FVector3d(0,-0.5,-1),FVector3d(0,-0.5,1),FVector3d(0,0.5,0));
	TestEqual(TEXT("True 3D crossing"), FindIssues(Crossing,Checks).Triangles.Num(), 2);
	FDynamicMesh3 SplitSeam;
	Triangle(SplitSeam,FVector3d(0,0,0),FVector3d(1,0,0),FVector3d(0,1,0));
	Triangle(SplitSeam,FVector3d(1,0,0),FVector3d(1,1,0),FVector3d(0,1,0));
	TestTrue(TEXT("Split-vertex seam touching without overlap is excluded"),FindIssues(SplitSeam,Checks).Triangles.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairQualityTest, "PCGUtils.DynMesh.RepairMesh.QualityChecks", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairQualityTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	FDynamicMesh3 M; Triangle(M,FVector3d(0,0,0),FVector3d(100,0,0),FVector3d(50,0.001,0));
	Triangle(M,FVector3d(0,10,0),FVector3d(1,10,0),FVector3d(2,10,0));
	FChecks C; C.bIntersections = false;
	TestEqual(TEXT("Collapsed triangle only by default"),FindIssues(M,C).Triangles.Num(),1);
	C.bSliverTriangles = true; TestEqual(TEXT("Long-edged sliver also found by shape quality"),FindIssues(M,C).Triangles.Num(),2);
	FDynamicMesh3 SliverFan = Fan(FVector3d(0.5,0.0001,0));
	FRepairOptions O; O.Checks.bSliverTriangles = true;
	const auto R = Repair(SliverFan,O);
	TestTrue(TEXT("Local coplanar slivers are removed"),R.RepairedPatches > 0 && R.UnresolvedTriangles.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairPatchTest, "PCGUtils.DynMesh.RepairMesh.LocalPatchAndAttributes", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairPatchTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	FDynamicMesh3 M = Fan(FVector3d(1.5,0.5,0),true); const FDynamicMesh3 Before = M;
	FRepairOptions Options; const auto R = Repair(M,Options);
	TestTrue(TEXT("A connected overlap cluster is repaired"),R.RepairedPatches > 0);
	TestTrue(TEXT("No unresolved issues"),R.UnresolvedTriangles.IsEmpty());
	TestEqual(TEXT("Only two triangles needed for the square"),M.TriangleCount(),2);
	TestEqual(TEXT("Same four exterior boundary edges"),M.EdgeCount(),5);
	for (int32 V = 0; V < 4; ++V) { TestTrue(TEXT("Exact boundary positions preserved"),M.GetVertex(V).Equals(Before.GetVertex(V),0)); }
	TestTrue(TEXT("Valid mesh and overlays"),M.CheckValidity(FDynamicMesh3::FValidityOptions(),EValidityCheckFailMode::ReturnOnly));
	for (int32 T : M.TriangleIndicesItr())
	{
		TestEqual(TEXT("Material preserved"),M.Attributes()->GetMaterialID()->GetValue(T),7);
		TestEqual(TEXT("Default group preserved"),M.GetTriangleGroup(T),3);
		TestEqual(TEXT("Extended group preserved"),M.Attributes()->GetPolygroupLayer(0)->GetValue(T),11);
		const FIndex3i V = M.GetTriangle(T);
		for (int32 VID : {V.A,V.B,V.C})
		{
			FVector2f UV; M.Attributes()->PrimaryUV()->GetElementAtVertex(T,VID,UV);
			TestTrue(TEXT("UVs preserved at unchanged boundary vertices"),UV.Equals(FVector2f(M.GetVertex(VID).X,M.GetVertex(VID).Y),1.e-6f));
		}
	}
	FDynamicMesh3 WithRemote = Fan();
	const int32 Remote = Triangle(WithRemote,FVector3d(10,0,0),FVector3d(11,0,0),FVector3d(10,1,0));
	const FIndex3i RemoteVertices = WithRemote.GetTriangle(Remote);
	Repair(WithRemote,Options);
	TestTrue(TEXT("Unrelated triangle retains exact vertex IDs"),WithRemote.IsTriangle(Remote) && WithRemote.GetTriangle(Remote) == RemoteVertices);
	TestEqual(TEXT("Remote patch retained while local fan is repaired"),WithRemote.TriangleCount(),3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairLimitsTest, "PCGUtils.DynMesh.RepairMesh.SafeLimits", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairLimitsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	FRepairOptions O;
	FDynamicMesh3 M = Fan(); TSet<int32> Empty;
	Repair(M,O,&Empty); TestEqual(TEXT("Explicit empty selection changes nothing"),M.TriangleCount(),4);
	TSet<int32> One; One.Add(0); const auto Limited = Repair(M,O,&One);
	TestEqual(TEXT("Selection cannot be expanded outside selected faces"),M.TriangleCount(),4);
	TestTrue(TEXT("Selection-bound failure remains inspectable"),!Limited.UnresolvedTriangles.IsEmpty());
	FDynamicMesh3 Curved = Fan(FVector3d(1.5,0.5,2)); O.PlanarityTolerance = 0.001;
	O.Checks.bSliverTriangles = true; O.Checks.MinimumQuality = 0.99;
	const auto CurvedBefore = Curved; const auto CurvedResult = Repair(Curved,O);
	TestEqual(TEXT("Nonplanar patch is not flattened"),Curved.TriangleCount(),CurvedBefore.TriangleCount());
	TestTrue(TEXT("Interior position unchanged"),Curved.GetVertex(4).Equals(CurvedBefore.GetVertex(4),0));
	TestTrue(TEXT("A detected nonplanar defect remains unresolved"),!CurvedResult.UnresolvedTriangles.IsEmpty());
	O.Checks.bSliverTriangles = false;
	FDynamicMesh3 MaterialSplit = Fan(FVector3d(1.5,0.5,0),true); MaterialSplit.Attributes()->GetMaterialID()->SetValue(1,9);
	const auto Protected = Repair(MaterialSplit,O);
	TestTrue(TEXT("Material boundary blocks repair"),Protected.RepairedPatches == 0 && !Protected.UnresolvedTriangles.IsEmpty());
	FDynamicMesh3 Collapsed = Fan(FVector3d(1,0.5,0)); O.PlanarityTolerance = 0.1;
	const auto Recovered = Repair(Collapsed,O);
	TestTrue(TEXT("Collapsed triangle is replaced without a hole"),Recovered.RepairedPatches > 0 && Recovered.UnresolvedTriangles.IsEmpty() && Collapsed.TriangleCount() == 2);
	FDynamicMesh3 Disconnected;
	Triangle(Disconnected,FVector3d(0,0,0),FVector3d(10,0,0),FVector3d(0,10,0));
	Triangle(Disconnected,FVector3d(1,1,0),FVector3d(2,1,0),FVector3d(1,2,0));
	const auto Ambiguous = Repair(Disconnected,O);
	TestEqual(TEXT("Disconnected overlapping surfaces are left intact"),Disconnected.TriangleCount(),2);
	TestEqual(TEXT("Both ambiguous faces remain unresolved"),Ambiguous.UnresolvedTriangles.Num(),2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairPlanarTest, "PCGUtils.DynMesh.RepairMesh.PlanarCleanup", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairPlanarTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	FDynamicMesh3 M = Fan(FVector3d(0.5,0.5,0)); FRepairOptions O;
	Repair(M,O); TestEqual(TEXT("Ordinary planar topology untouched by default"),M.TriangleCount(),4);
	O.bPlanarCleanup = true; Repair(M,O);
	TestEqual(TEXT("Optional planar cleanup removes redundant interior vertex"),M.TriangleCount(),2);
	for (int32 V = 0; V < 4; ++V) { TestTrue(TEXT("Outline vertices retained"),M.IsVertex(V)); }
	FDynamicMesh3 ColorSeam = Fan(FVector3d(0.5,0.5,0),true);
	auto* Colors = ColorSeam.Attributes()->PrimaryColors();
	FIndex3i Elements = Colors->GetTriangle(0); Elements.C = Colors->AppendElement(FVector4f(1,0,1,1)); Colors->SetTriangle(0,Elements);
	Repair(ColorSeam,O);
	TestEqual(TEXT("Planar cleanup also protects color seams"),ColorSeam.TriangleCount(),4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairSelectorTest, "PCGUtils.DynMesh.RepairMesh.SelectorAndDomains", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairSelectorTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	auto* Data = NewObject<UPCGDynamicMeshData>(); Data->Initialize(Fan());
	auto* Settings = NewObject<UPCGDynMeshIssuesSelectionSettings>();
	auto* Factory = CastChecked<UPCGDynMeshIssuesSelectionFactoryData>(Settings->CreateFactory(nullptr));
	TestTrue(TEXT("Native faces selected"),!Evaluate(Factory,Data).IsEmpty());
	TestTrue(TEXT("Vertex consumer adapts the face predicate"),!Evaluate(Factory,Data,EGeometryElementType::Vertex).IsEmpty());
	TestTrue(TEXT("Edge consumer adapts the face predicate"),!Evaluate(Factory,Data,EGeometryElementType::Edge).IsEmpty());
	const uint32 CRC = Factory->GetOrComputeCrc(true).GetValue();
	auto* Other = NewObject<UPCGDynMeshIssuesSelectionFactoryData>(); Other->Checks.bSliverTriangles = true;
	TestNotEqual(TEXT("Check options affect cache identity"),CRC,Other->GetOrComputeCrc(true).GetValue());
	FChecks Resolved; FText Error; Settings->Checks.IntersectionTolerance.Input = EPCGUtilsInputValueType::Attribute;
	TestFalse(TEXT("Missing @Data threshold cannot silently use the constant"),ResolveChecks(Settings->Checks,Data,Resolved,Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRepairPipelineTest, "PCGUtils.DynMesh.RepairMesh.PCGPipeline", PCGUtilsDynMeshRepairMeshTests::Flags)
bool FPCGDynMeshRepairPipelineTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRepairMeshTests;
	auto* Data = NewObject<UPCGDynamicMeshData>(); Data->Initialize(Fan());
	auto* Settings = NewObject<UPCGDynMeshRepairMeshSettings>();
	auto* SelectorSettings = NewObject<UPCGDynMeshIssuesSelectionSettings>(); SelectorSettings->Checks.MinimumArea.Constant = 100;
	auto* Selector = SelectorSettings->CreateFactory(nullptr);
	const auto Output = Run(Settings,Data,Selector);
	const auto* Main = Output.FindByPredicate([](const auto& D) { return D.Pin == TEXT("Out"); });
	const auto* Unresolved = Output.FindByPredicate([](const auto& D) { return D.Pin == TEXT("Unresolved"); });
	TestNotNull(TEXT("Primary mesh output"),Main); TestNotNull(TEXT("Unresolved selection output"),Unresolved);
	if (Main)
	{
		const auto* Result = Cast<UPCGDynamicMeshData>(Main->Data);
		TestNotNull(TEXT("Out remains mesh data"),Result);
		TestTrue(TEXT("Input tags retained"),Main->Tags.Contains(TEXT("RepairTest")));
		if (Result) { TestTrue(TEXT("Selector scopes repair without losing mesh"),FindIssues(*Result->GetDynamicMesh()->GetMeshPtr(),FChecks()).Triangles.IsEmpty()); }
	}
	TestEqual(TEXT("Source mesh is not mutated"),Data->GetDynamicMesh()->GetMeshPtr()->TriangleCount(),4);
	const auto Inputs = Settings->AllInputPinProperties();
	TestTrue(TEXT("Optional Selector pin present"),Inputs.ContainsByPredicate([](const auto& P) { return P.Label == TEXT("Selector"); }));
	TestEqual(TEXT("Mutated mesh input comes first"),Inputs[0].Label,FName(TEXT("In")));
	TestTrue(TEXT("Primary input accepts meshes and selections"),Inputs[0].AllowedTypes.Intersects(FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh))
		&& Inputs[0].AllowedTypes.Intersects(FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass())));
	return true;
}
#endif
