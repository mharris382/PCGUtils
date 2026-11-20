// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CompGeom/PolygonTriangulation.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Topology/PCGBevelEdges.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "Geometry/PCGUtilsDynMeshBevelClamp.h"
#include "Geometry/PCGUtilsDynMeshFaceAngles.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Operations/InsetMeshRegion.h"
#include "Operations/MeshBevel.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshBevelClampTests
{
	using namespace UE::Geometry;
	using EMode = EPCGUtilsDynMeshAmountClampMode;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	UPCGDynamicMeshData* Box(double X = 100, double Y = 100, double Z = 100)
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
			FTransform::Identity, X, Y, Z, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}
	const FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data) { return *Data->GetDynamicMesh()->GetMeshPtr(); }
	TArray<int32> Creases(const FDynamicMesh3& M)
	{
		const auto C = PCGUtilsDynMeshFaceAngles::MakeDefaultComparison();
		TArray<int32> Edges;
		for (int32 E : M.EdgeIndicesItr()) { if (PCGUtilsDynMeshFaceAngles::TestEdge(M, E, C, C.GetConstantDot())) { Edges.Add(E); } }
		return Edges;
	}
	bool Healthy(const FDynamicMesh3& M)
	{
		for (int32 T : M.TriangleIndicesItr()) { if (!(M.GetTriArea(T) > 0.0)) { return false; } }
		FDynamicMeshAABBTree3 Tree(&M, true);
		const auto I = Tree.FindAllSelfIntersections(true, IMeshSpatial::FQueryOptions(),
			[](FIntrTriangle3Triangle3d& Intr) { Intr.SetReportCoplanarIntersection(true); return Intr.Find(); });
		return I.Points.IsEmpty() && I.Segments.IsEmpty() && I.Polygons.IsEmpty();
	}
	FDynamicMesh3 LPrism(TArray<int32>& TopEdges)
	{
		const TArray<FVector3d> Ring = { {0,0,0}, {100,0,0}, {100,2,0}, {20,2,0}, {20,100,0}, {0,100,0} };
		FDynamicMesh3 M; M.EnableTriangleGroups(0);
		for (const auto& P : Ring) { M.AppendVertex(P); }
		for (const auto& P : Ring) { M.AppendVertex(P + FVector3d(0,0,30)); }
		TArray<FIndex3i> Cap;
		PolygonTriangulation::TriangulateSimplePolygon<double>(Ring, Cap);
		for (const auto& T : Cap)
		{
			int32 Bottom = M.AppendTriangle(T.A, T.B, T.C, 0);
			if (M.GetTriNormal(Bottom).Z > 0) { M.ReverseTriOrientation(Bottom); }
			int32 Top = M.AppendTriangle(T.A + 6, T.B + 6, T.C + 6, 1);
			if (M.GetTriNormal(Top).Z < 0) { M.ReverseTriOrientation(Top); }
		}
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 J = (I + 1) % 6;
			const FVector3d E = Ring[J] - Ring[I], Outward(E.Y, -E.X, 0);
			const int32 A = M.AppendTriangle(I, J, J + 6, I + 2), B = M.AppendTriangle(I, J + 6, I + 6, I + 2);
			if (M.GetTriNormal(A).Dot(Outward) < 0) { M.ReverseTriOrientation(A); M.ReverseTriOrientation(B); }
			TopEdges.Add(M.FindEdge(I + 6, J + 6));
		}
		M.EnableAttributes(); M.Attributes()->EnableMaterialID();
		for (int32 TID : M.TriangleIndicesItr())
		{
			const auto V = M.GetTriangle(TID); FIndex3i N, UV;
			for (int32 C = 0; C < 3; ++C)
			{
				N[C] = M.Attributes()->PrimaryNormals()->AppendElement(FVector3f(M.GetTriNormal(TID)));
				const auto P = M.GetVertex(V[C]); UV[C] = M.Attributes()->PrimaryUV()->AppendElement(FVector2f(float(P.X), float(P.Y)));
			}
			M.Attributes()->PrimaryNormals()->SetTriangle(TID, N); M.Attributes()->PrimaryUV()->SetTriangle(TID, UV);
		}
		return M;
	}
	TArray<FPCGTaggedData> Run(UPCGBevelEdgesSettings* Settings, const UPCGData* Data)
	{
		FPCGDataCollection Input; auto& D = Input.TaggedData.Emplace_GetRef(); D.Pin = TEXT("Mesh"); D.Data = Data; D.Tags.Add(TEXT("SourceTag"));
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		const auto Element = Settings->GetElement(); TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true; Context->AsyncState.NumAvailableTasks = 1;
		for (int32 I = 0; I < 10; ++I) { if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; } }
		return {};
	}
	const UPCGData* OnPin(const TArray<FPCGTaggedData>& Out, FName Pin)
	{ const auto* D = Out.FindByPredicate([Pin](const auto& Item) { return Item.Pin == Pin; }); return D ? D->Data.Get() : nullptr; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampSafeAndThinTest, "PCGUtils.DynMesh.BevelClamp.SafeAndThin", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampSafeAndThinTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	const auto* Data = Box(2,100,100); const auto Edges = Creases(Mesh(Data));
	for (int32 Subdivisions : { 0, 2 })
	{
		FDynamicMesh3 Result(Mesh(Data)); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
		PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 10; O.Subdivisions = Subdivisions;
		TestTrue(TEXT("Thin-face clamp executes"), PCGUtilsDynMeshBevelClamp::Apply(Result, Edges, O, Added, &D));
		AddInfo(FString::Printf(TEXT("Thin: skipped %d, reason %s"), D.SkippedEdges, *D.FailureReason));
		TestFalse(TEXT("Thin-face solve finds a usable amount"), D.bCouldNotFindSafeAmount);
		TestTrue(TEXT("Clamp keeps a real bevel"), !Added.IsEmpty());
		TestTrue(TEXT("Two-unit thickness limits conflicting offsets before collapse"), D.MaximumAmount < 2 && D.MinimumAmount > 0);
		TestTrue(TEXT("Clamped chamfer/rounded bevel has no new intersections or zero-area faces"), Healthy(Result));
	}
	FDynamicMesh3 Safe(Mesh(Data)), Expected(Mesh(Data)); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
	PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 0.25;
	PCGUtilsDynMeshBevelClamp::Apply(Safe, Edges, O, Added, &D);
	FMeshBevel Raw; Raw.InsetDistance = O.Distance;
	Raw.InitializeFromTriangleEdges(Expected, Edges, [&Expected](int32 V) { return Expected.HasTriangleGroups() && Expected.IsGroupJunctionVertex(V); }); Raw.Apply(Expected, nullptr);
	TestEqual(TEXT("A safe requested amount is retained exactly"), D.MinimumAmount, O.Distance);
	TestTrue(TEXT("Safe amount matches the engine's geometry"), Safe.IsSameAs(Expected, FDynamicMesh3::FSameAsOptions()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampModesTest, "PCGUtils.DynMesh.BevelClamp.SelectionIslands", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampModesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	auto* Data = Box(2,100,100);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
		FTransform(FVector(300,0,0)), 100,100,100,0,0,0,EGeometryScriptPrimitiveOriginMode::Center);
	for (EMode Mode : { EMode::WholeMesh, EMode::SelectionIsland, EMode::Regional })
	{
		FDynamicMesh3 M(Mesh(Data)); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
		PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 10; O.Mode = Mode;
		PCGUtilsDynMeshBevelClamp::Apply(M, Creases(M), O, Added, &D);
		TestFalse(TEXT("Each mode finds safe geometry"), D.bCouldNotFindSafeAmount);
		TestEqual(TEXT("Selection islands follow selected connectivity"), D.SelectionIslands, 2);
		TestTrue(TEXT("Thin island is limited"), D.MinimumAmount < 1);
		if (Mode == EMode::WholeMesh) { TestEqual(TEXT("Whole mesh has one amount"), D.MinimumAmount, D.MaximumAmount); }
		else { TestEqual(TEXT("Wide island keeps the requested amount"), D.MaximumAmount, 10.0); }
		TestTrue(TEXT("Each mode produces valid nonoverlapping geometry"), Healthy(M));
	}
	FDynamicMesh3 Connected(Mesh(Box())); const auto AllCreases = Creases(Connected); TArray<int32> SeparateEdges = { AllCreases[0] };
	const auto A = Connected.GetEdgeV(SeparateEdges[0]);
	for (int32 E : AllCreases)
	{
		const auto B = Connected.GetEdgeV(E);
		if (B.A != A.A && B.A != A.B && B.B != A.A && B.B != A.B) { SeparateEdges.Add(E); break; }
	}
	TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D; PCGUtilsDynMeshBevelClamp::FOptions O;
	O.Distance = 0.25; O.Mode = EMode::SelectionIsland;
	PCGUtilsDynMeshBevelClamp::Apply(Connected, SeparateEdges, O, Added, &D);
	TestEqual(TEXT("Unselected edges on one connected mesh do not merge selection islands"), D.SelectionIslands, 2);
	TestFalse(TEXT("Disconnected selected edges still produce a usable bevel"), D.bCouldNotFindSafeAmount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampRegionalTest, "PCGUtils.DynMesh.BevelClamp.RegionalConnectedLoop", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampRegionalTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	TArray<int32> Edges; const FDynamicMesh3 Source = LPrism(Edges);
	TestTrue(TEXT("The notched fixture starts valid"), Healthy(Source));
	for (EMode Mode : { EMode::SelectionIsland, EMode::Regional })
	for (int32 Subdivisions : { 0, 2 })
	{
		FDynamicMesh3 M(Source); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
		PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 8; O.Mode = Mode; O.Subdivisions = Subdivisions; O.MaterialID = 7;
		PCGUtilsDynMeshBevelClamp::Apply(M, Edges, O, Added, &D);
		AddInfo(FString::Printf(TEXT("Loop %d subdivisions %d: skipped %d, reason %s"), int32(Mode), Subdivisions, D.SkippedEdges, *D.FailureReason));
		TestFalse(TEXT("Connected loop resolves a usable clamp"), D.bCouldNotFindSafeAmount);
		TestEqual(TEXT("The entire rim is one selected island"), D.SelectionIslands, 1);
		TestTrue(TEXT("The narrow branch is clamped"), D.MinimumAmount < 2);
		if (Mode == EMode::Regional) { TestTrue(TEXT("Regional retains a wider bevel elsewhere on the same connected loop"), D.MaximumAmount > D.MinimumAmount * 2); }
		else { TestEqual(TEXT("Island mode keeps that loop uniform"), D.MaximumAmount, D.MinimumAmount); }
		TestTrue(TEXT("Transitions do not fold or intersect"), Healthy(M));
		TestTrue(TEXT("Regional attributes remain valid"), M.CheckValidity());
		for (int32 TID : Added)
		{
			TestEqual(TEXT("Regional template preserves generated material IDs"), M.Attributes()->GetMaterialID()->GetValue(TID), 7);
			TestTrue(TEXT("Generated bevel has normals and UVs after local displacement"), M.Attributes()->PrimaryNormals()->IsSetTriangle(TID) && M.Attributes()->PrimaryUV()->IsSetTriangle(TID));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampInsetLibraryTest, "PCGUtils.DynMesh.BevelClamp.SharedInsetLibrary", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampInsetLibraryTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	const auto* Data = Box(2,100,100); const auto& Source = Mesh(Data);
	TArray<int32> Cap; TSet<int32> Verts;
	for (int32 T : Source.TriangleIndicesItr())
	{
		if (Source.GetTriNormal(T).Z < 0.99) { continue; }
		Cap.Add(T); const auto V = Source.GetTriangle(T); Verts.Add(V.A); Verts.Add(V.B); Verts.Add(V.C);
	}
	PCGUtilsDynMeshAmountClamp::FSelectionGraph Graph; Graph.Vertices = Verts.Array();
	for (int32 E : Source.EdgeIndicesItr()) { const auto V = Source.GetEdgeV(E); if (Verts.Contains(V.A) && Verts.Contains(V.B)) { Graph.Edges.Add({ V.A,V.B }); } }
	auto Inset = [&Cap](double Amount, PCGUtilsDynMeshAmountClamp::FSample& S)
	{
		FInsetMeshRegion Op(&S.Mesh); Op.Triangles = Cap; Op.InsetDistance = Amount; Op.bReproject = false; Op.bSolveRegionInteriors = false;
		if (!Op.Apply()) { return false; }
		for (const auto& R : Op.InsetRegions) { S.ResultTriangles.Append(R.InitialTriangles); for (const auto& Strip : R.StitchTriangles) { S.AddedTriangles.Append(Strip); } }
		return true;
	};
	PCGUtilsDynMeshAmountClamp::FSample Out; PCGUtilsDynMeshAmountClamp::FResult D;
	TestTrue(TEXT("The shared resolver runs the real Inset operation without a Bevel dependency"),
		PCGUtilsDynMeshAmountClamp::Resolve(Source, Graph, 20, EMode::Regional, Inset, Out, D));
	TestFalse(TEXT("Inset has a usable local amount"), D.bCouldNotFindSafeAmount);
	TestTrue(TEXT("Inset preserves a nonempty cap and border"), !Out.ResultTriangles.IsEmpty() && !Out.AddedTriangles.IsEmpty());
	TestTrue(TEXT("Inset stops before its two-unit cap collapses"), D.MaximumAmount < 2 && D.MinimumAmount > 0);
	TestTrue(TEXT("The limited inset has valid geometry"), Healthy(Out.Mesh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampNodeTest, "PCGUtils.DynMesh.BevelClamp.NodeAndDeferred", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampNodeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	auto* Data = Box(2,100,100); auto* Settings = NewObject<UPCGBevelEdgesSettings>();
	TestTrue(TEXT("Overlap protection defaults on"), Settings->bClampOverlap);
	TestEqual(TEXT("Per Vertex is the default mode (same serialized value as Regional)"), Settings->ClampMode, EMode::Regional);
	Settings->bFilterByFaceAngle = true; Settings->BevelDistance.Constant = 10; Settings->bOutputSelectionData = true;
	const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(OnPin(Run(Settings, Data), TEXT("Out")));
	if (!TestNotNull(TEXT("Node emits its processed result selection"), Selection)) { return false; }
	TestTrue(TEXT("Node preserves an actual bevel result"), !Selection->GetSelection().IsEmpty());
	TestTrue(TEXT("Node applies the clamp"), Healthy(Mesh(Selection->GetSourceMeshData())));
	Settings->bClampOverlap = false;
	const auto* Unclamped = Cast<UPCGDynamicMeshSelectionData>(OnPin(Run(Settings, Data), TEXT("Out")));
	FDynamicMesh3 RawMesh(Mesh(Data)); FMeshBevel Raw; Raw.InsetDistance = 10;
	Raw.InitializeFromTriangleEdges(RawMesh, Creases(RawMesh), [&RawMesh](int32 V) { return RawMesh.IsGroupJunctionVertex(V); }); Raw.Apply(RawMesh, nullptr);
	TestTrue(TEXT("Disabling Clamp Overlap preserves the native requested bevel"), Unclamped && Mesh(Unclamped->GetSourceMeshData()).IsSameAs(RawMesh, FDynamicMesh3::FSameAsOptions()));
	Settings->bClampOverlap = true;
	Settings->BevelDistance.Constant = 0;
	const auto* Zero = Cast<UPCGDynamicMeshSelectionData>(OnPin(Run(Settings, Data), TEXT("Out")));
	TestTrue(TEXT("Zero distance is a clean no-op"), Zero && Zero->GetSelection().IsEmpty() && Mesh(Zero->GetSourceMeshData()).TriangleCount() == Mesh(Data).TriangleCount());
	Settings->BevelDistance.Constant = 10; Settings->bOutputSelectionData = false;
	auto* Primitive = NewObject<UPCGCreatePrimitiveBoxSettings>(); Primitive->DimensionX = 2; Primitive->DimensionY = Primitive->DimensionZ = 100; Primitive->Origin = EGeometryScriptPrimitiveOriginMode::Center;
	auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>(); Builder->Primitive = Primitive; Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
	const auto* Deferred = Cast<UPCGUtilsDynMeshProcessBuilderFactoryData>(OnPin(Run(Settings, Builder), TEXT("Out")));
	if (!TestNotNull(TEXT("Clamp supports deferred Builders"), Deferred)) { return false; }
	FPCGUtilsDynMeshBuildContext Seed; Seed.SeedLocalBounds = FBox(FVector(-50), FVector(50)); FPCGUtilsDynMeshBuildResult Built;
	TestTrue(TEXT("Deferred clamp materializes"), Deferred->CreateOperation(nullptr)->Build(Seed, Built));
	TestTrue(TEXT("Deferred clamp creates valid beveled geometry"), Built.MeshData && Healthy(Mesh(Built.MeshData)) && Mesh(Built.MeshData).TriangleCount() > 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampFallbackTest, "PCGUtils.DynMesh.BevelClamp.UnresolvedPreservesInput", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampFallbackTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	const auto* Data = Box(); const auto& Source = Mesh(Data);
	PCGUtilsDynMeshAmountClamp::FSelectionGraph Graph; Graph.Vertices.Add(0);
	PCGUtilsDynMeshAmountClamp::FSample Out; PCGUtilsDynMeshAmountClamp::FResult D;
	TestTrue(TEXT("An unresolved heuristic is a pass-through, not an operation failure"),
		PCGUtilsDynMeshAmountClamp::Resolve(Source, Graph, 1, EMode::Regional,
			[](double, PCGUtilsDynMeshAmountClamp::FSample& S) { S.Mesh.Clear(); return false; }, Out, D));
	TestTrue(TEXT("Fallback reports its reason"), D.bCouldNotFindSafeAmount && !D.FailureReason.IsEmpty());
	TestTrue(TEXT("Fallback restores the entire source with attributes"), Out.Mesh.IsSameAs(Source, FDynamicMesh3::FSameAsOptions()));
	TestTrue(TEXT("Fallback result selection is empty"), Out.ResultTriangles.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampEndpointTest, "PCGUtils.DynMesh.BevelClamp.IndependentEndpoints", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampEndpointTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	TArray<int32> Edges; const auto Source = LPrism(Edges);
	FDynamicMesh3 M(Source); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
	PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 8;
	PCGUtilsDynMeshBevelClamp::Apply(M, Edges, O, Added, &D);
	TestFalse(TEXT("The connected rim retains a real bevel"), D.bCouldNotFindSafeAmount);
	TestEqual(TEXT("Small but usable endpoints do not remove edges"), D.SkippedEdges, 0);
	TestEqual(TEXT("A safe endpoint retains the complete requested amount"), D.MaximumAmount, 8.0);
	bool bIndependent = false;
	for (int32 E : Edges)
	{
		const auto V = Source.GetEdgeV(E); const double* A = D.VertexAmounts.Find(V.A); const double* B = D.VertexAmounts.Find(V.B);
		if (A && B) { bIndependent |= (*A == 8 && *B < 2) || (*B == 8 && *A < 2); }
	}
	TestTrue(TEXT("One endpoint can stay full width while the other is limited"), bIndependent);
	TestTrue(TEXT("No zero-width degenerates are used to terminate an endpoint"), Healthy(M) && M.CheckValidity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampPartialTest, "PCGUtils.DynMesh.BevelClamp.MinimumKeepsHealthyEdges", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampPartialTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	auto* Data = Box(0.02,100,100);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
		FTransform(FVector(300,0,0)),100,100,100,0,0,0,EGeometryScriptPrimitiveOriginMode::Center);
	FDynamicMesh3 M(Mesh(Data)); const auto Edges = Creases(M); TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D;
	PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 10; O.MinimumDistance = 0.05;
	PCGUtilsDynMeshBevelClamp::Apply(M, Edges, O, Added, &D);
	TestFalse(TEXT("An unusable thin section does not cancel the mesh"), D.bCouldNotFindSafeAmount);
	TestTrue(TEXT("Only part of the selection is omitted"), D.SkippedEdges > 0 && D.SkippedEdges < Edges.Num());
	TestTrue(TEXT("The surviving section has generated bevel faces"), !Added.IsEmpty());
	TestEqual(TEXT("Healthy edges keep their requested amount"), D.MaximumAmount, 10.0);
	TestTrue(TEXT("The final partial bevel has valid geometry"), Healthy(M) && M.CheckValidity());
	AddInfo(FString::Printf(TEXT("Omitted %d/%d edges; retained offset range %.6f..%.6f"), D.SkippedEdges, Edges.Num(), D.MinimumAmount, D.MaximumAmount));
	TArray<int32> Rim; const auto Connected = LPrism(Rim); FDynamicMesh3 Local(Connected); O.Distance = 8; O.MinimumDistance = 2;
	PCGUtilsDynMeshBevelClamp::Apply(Local, Rim, O, Added, &D);
	TestFalse(TEXT("Omitting unusable endpoints preserves part of a connected rim"), D.bCouldNotFindSafeAmount);
	TestTrue(TEXT("Only the constrained part of the connected selection is omitted"), D.SkippedEdges > 0 && D.SkippedEdges < Rim.Num());
	TestTrue(TEXT("The connected selection keeps actual bevel faces"), !Added.IsEmpty());
	TestEqual(TEXT("Unconstrained endpoints on that same mesh stay full width"), D.MaximumAmount, 8.0);
	TestTrue(TEXT("Locally omitted edges leave valid termination topology"), Healthy(Local) && Local.CheckValidity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampStaticContactTest, "PCGUtils.DynMesh.BevelClamp.UnchangedBowtieContact", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampStaticContactTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	FDynamicMesh3 M(Mesh(Box())); const auto Edges = Creases(M);
	const int32 A = M.AppendVertex({1000,0,0}), B = M.AppendVertex({1010,0,0}), C = M.AppendVertex({1000,10,0});
	const int32 D0 = M.AppendVertex({990,0,0}), E = M.AppendVertex({1000,-10,0});
	M.AppendTriangle(A,B,C); M.AppendTriangle(A,D0,E);
	TArray<int32> Added; PCGUtilsDynMeshBevelClamp::FResult D; PCGUtilsDynMeshBevelClamp::FOptions O; O.Distance = 5;
	PCGUtilsDynMeshBevelClamp::Apply(M, Edges, O, Added, &D);
	TestFalse(TEXT("Untouched bowtie contact cannot trigger the whole-mesh fallback"), D.bCouldNotFindSafeAmount);
	TestTrue(TEXT("Selected box still produces bevel geometry"), !Added.IsEmpty());
	TestEqual(TEXT("No healthy edge is omitted"), D.SkippedEdges, 0);
	TestEqual(TEXT("No healthy endpoint loses any amount"), D.MinimumAmount, 5.0);
	// The same old contact is harmless even when another corner of the retained face is actually beveled.
	FDynamicMesh3 Contact(Mesh(Box())); const auto All = Creases(Contact); int32 Selected = INDEX_NONE;
	for (int32 Edge : All)
	{
		const auto V = Contact.GetEdgeV(Edge), T = Contact.GetEdgeT(Edge);
		if (V.A == 0 || V.B == 0) { continue; }
		const auto A0 = Contact.GetTriangle(T.A), B0 = Contact.GetTriangle(T.B);
		if (A0.A == 0 || A0.B == 0 || A0.C == 0 || B0.A == 0 || B0.B == 0 || B0.C == 0) { Selected = Edge; break; }
	}
	if (!TestTrue(TEXT("A selected edge shares a retained face with the untouched contact"), Selected != INDEX_NONE)) { return false; }
	const auto Corner = Contact.GetVertex(0);
	const int32 P1 = Contact.AppendVertex(Corner + FVector3d(FMath::Sign(Corner.X) * 1000,0,0));
	const int32 P2 = Contact.AppendVertex(Corner + FVector3d(0,FMath::Sign(Corner.Y) * 1000,0));
	Contact.AppendTriangle(0,P1,P2);
	PCGUtilsDynMeshBevelClamp::Apply(Contact, TArray<int32>{Selected}, O, Added, &D);
	TestFalse(TEXT("An original point contact does not constrain the opposite corner"), D.bCouldNotFindSafeAmount);
	TestTrue(TEXT("The edge on the changed face is still beveled"), !Added.IsEmpty());
	TestEqual(TEXT("The safe changed face keeps its full offset"), D.MinimumAmount, 5.0);
	TestEqual(TEXT("The safe edge is not omitted"), D.SkippedEdges, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshClampMinimumAttributeTest, "PCGUtils.DynMesh.BevelClamp.MinimumDistanceInput", PCGUtilsDynMeshBevelClampTests::Flags)
bool FPCGDynMeshClampMinimumAttributeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelClampTests;
	auto* Data = Box(); auto* Settings = NewObject<UPCGBevelEdgesSettings>(); Settings->bFilterByFaceAngle = true;
	Settings->MinimumBevelDistance.Input = EPCGUtilsInputValueType::Attribute;
	TestFalse(TEXT("Attribute minimum refuses unrealized Builders"), Settings->SupportsDeferredBuilderProcessing());
	AddExpectedError(TEXT("Minimum Bevel Distance (input 0):"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("Missing minimum does not use a constant fallback"), OnPin(Run(Settings, Data), TEXT("Out")));
	auto* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
	auto* Attribute = Domain->FindOrCreateAttribute<double>(TEXT("MinimumBevelDistance"),0.01,false,false,true);
	Domain->AddEntry(); Attribute->SetValue(PCGFirstEntryKey, 0.01);
	TestNotNull(TEXT("Per-mesh minimum can be read"), OnPin(Run(Settings, Data), TEXT("Out")));
	Settings->bClampOverlap = false;
	TestTrue(TEXT("Disabled clamp ignores the minimum's Builder restriction"), Settings->SupportsDeferredBuilderProcessing());
	return true;
}

#endif
