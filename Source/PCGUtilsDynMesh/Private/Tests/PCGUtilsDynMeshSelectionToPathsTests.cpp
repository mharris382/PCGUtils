// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/Conversion/PCGDynamicMeshSelectionToPaths.h"
#include "Elements/Topology/PCGDynMeshExtrudeToPaths.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Geometry/PCGUtilsDynMeshRimToPath.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "OrientedBoxTypes.h"
#include "PCGContext.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshSelectionToPathsTests
{
	/** A box, with the faces whose normal points up selected: one region with one boundary loop. */
	UPCGDynamicMeshSelectionData* TopOfBox()
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(100, 60, 10));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(5, 4, 2);
		UE::Geometry::FDynamicMesh3 Mesh(&Generator.Generate());

		UE::Geometry::FGeometrySelection Selection;
		Selection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriNormal(TriangleID).Z > 0.9)
			{
				Selection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}

		UPCGDynamicMeshData* MeshData = NewObject<UPCGDynamicMeshData>();
		MeshData->Initialize(MoveTemp(Mesh));
		UPCGDynamicMeshSelectionData* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
		SelectionData->Initialize(MeshData, MoveTemp(Selection));
		return SelectionData;
	}

	TArray<FPCGTaggedData> Run(UPCGDynamicMeshSelectionToPathsSettings* Settings, const UPCGData* Selection)
	{
		FPCGDataCollection Input;
		FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
		Tagged.Data = Selection;
		Tagged.Pin = TEXT("Selection");
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData.GetInputsByPin(TEXT("Paths")); }
		}
		return {};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectionToPathsVertexIndexTest,
	"PCGUtils.DynMesh.SelectionToPaths.EachPathPointNamesItsMeshVertex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSelectionToPathsVertexIndexTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	const UPCGDynamicMeshSelectionData* Selection = TopOfBox();
	const UE::Geometry::FDynamicMesh3& Mesh = Selection->GetSourceMeshData()->GetDynamicMesh()->GetMeshRef();

	UPCGDynamicMeshSelectionToPathsSettings* Settings = NewObject<UPCGDynamicMeshSelectionToPathsSettings>();
	Settings->bOutputToWorldSpace = false;

	// Off by default: the points carry no vertex attribute.
	{
		const TArray<FPCGTaggedData> Paths = Run(Settings, Selection);
		UTEST_EQUAL(TEXT("The top of a box has one boundary loop"), Paths.Num(), 1);
		const UPCGBasePointData* Path = Cast<UPCGBasePointData>(Paths[0].Data);
		UTEST_NOT_NULL(TEXT("The path is point data"), Path);
		TestNull(TEXT("With the option off there is no VertexIndex attribute"), Path->ConstMetadata()->GetConstAttribute(TEXT("VertexIndex")));
	}

	Settings->bOutputVertexIndex = true;
	Settings->VertexIndexAttribute = TEXT("Vtx");
	const TArray<FPCGTaggedData> Paths = Run(Settings, Selection);
	UTEST_EQUAL(TEXT("Still one boundary loop"), Paths.Num(), 1);
	const UPCGBasePointData* Path = Cast<UPCGBasePointData>(Paths[0].Data);
	UTEST_NOT_NULL(TEXT("The path is point data"), Path);
	const FPCGMetadataAttribute<int32>* VertexIndex = Path->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("Vtx"));
	UTEST_NOT_NULL(TEXT("The points carry the attribute under the name asked for"), VertexIndex);

	// 5 x 4 vertices along the top's edges: a rim of 2 * (5 + 4) - 4 = 14.
	TestEqual(TEXT("The loop has one point per rim vertex"), Path->GetNumPoints(), 14);
	const auto Transforms = Path->GetConstTransformValueRange();
	const auto Entries = Path->GetConstMetadataEntryValueRange();
	TSet<int32> Seen;
	int32 Wrong = 0;
	for (int32 Index = 0; Index < Path->GetNumPoints(); ++Index)
	{
		const int32 VertexID = VertexIndex->GetValueFromItemKey(Entries[Index]);
		Seen.Add(VertexID);
		if (!Mesh.IsVertex(VertexID) || !FVector(Mesh.GetVertex(VertexID)).Equals(Transforms[Index].GetLocation(), 1.e-4))
		{
			++Wrong;
		}
	}
	TestEqual(TEXT("Every point names the mesh vertex it sits on"), Wrong, 0);
	TestEqual(TEXT("No vertex is named twice"), Seen.Num(), Path->GetNumPoints());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectionToPathsDataDomainTest,
	"PCGUtils.DynMesh.SelectionToPaths.PathsKeepTheMeshDataAttributes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSelectionToPathsDataDomainTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	const UPCGDynamicMeshSelectionData* Selection = TopOfBox();
	UPCGDynamicMeshData* MeshData = const_cast<UPCGDynamicMeshData*>(Selection->GetSourceMeshData());
	FPCGMetadataDomain* Domain = MeshData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
	Domain->AddEntry();
	Domain->FindOrCreateAttribute<int32>(TEXT("GroupID"), 0, false, false, true)->SetValue(PCGFirstEntryKey, 7);
	// The mesh says it is open; the path is a closed loop, and the node's answer wins.
	Domain->FindOrCreateAttribute<bool>(TEXT("IsClosed"), false, false, false, true)->SetValue(PCGFirstEntryKey, false);

	UPCGDynamicMeshSelectionToPathsSettings* Settings = NewObject<UPCGDynamicMeshSelectionToPathsSettings>();
	Settings->bOutputToWorldSpace = false;
	const TArray<FPCGTaggedData> Paths = Run(Settings, Selection);
	UTEST_EQUAL(TEXT("The top of a box has one boundary loop"), Paths.Num(), 1);
	const UPCGMetadata* Metadata = Paths[0].Data ? Paths[0].Data->ConstMetadata() : nullptr;
	UTEST_NOT_NULL(TEXT("The path has metadata"), Metadata);

	const FPCGMetadataAttribute<int32>* GroupID =
		Metadata->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(TEXT("GroupID"), PCGMetadataDomainID::Data));
	UTEST_NOT_NULL(TEXT("The path carries the mesh's @Data GroupID"), GroupID);
	TestEqual(TEXT("With the mesh's value"), GroupID->GetValueFromItemKey(PCGFirstEntryKey), 7);

	const FPCGMetadataAttribute<bool>* IsClosed =
		Metadata->GetConstTypedAttribute<bool>(FPCGAttributeIdentifier(TEXT("IsClosed"), PCGMetadataDomainID::Data));
	UTEST_NOT_NULL(TEXT("The path has IsClosed"), IsClosed);
	TestTrue(TEXT("IsClosed is the node's answer, not the mesh's"), IsClosed->GetValueFromItemKey(PCGFirstEntryKey));

	Settings->IsClosedAttribute = TEXT("Loop");
	const TArray<FPCGTaggedData> Renamed = Run(Settings, Selection);
	UTEST_EQUAL(TEXT("Still one boundary loop"), Renamed.Num(), 1);
	const FPCGMetadataAttribute<bool>* Loop = Renamed[0].Data->ConstMetadata()->GetConstTypedAttribute<bool>(
		FPCGAttributeIdentifier(TEXT("Loop"), PCGMetadataDomainID::Data));
	UTEST_NOT_NULL(TEXT("The closed flag is written under the name asked for"), Loop);
	TestTrue(TEXT("And is true"), Loop->GetValueFromItemKey(PCGFirstEntryKey));
	return true;
}

namespace PCGUtilsDynMeshSelectionToPathsTests
{
	FPCGDataCollection RunExtrude(UPCGDynMeshExtrudeToPathsSettings* Settings, const UPCGData* Selection, const UPCGData* Paths)
	{
		FPCGDataCollection Input;
		FPCGTaggedData& Mesh = Input.TaggedData.Emplace_GetRef();
		Mesh.Data = Selection;
		Mesh.Pin = TEXT("In");
		FPCGTaggedData& Path = Input.TaggedData.Emplace_GetRef();
		Path.Data = Paths;
		Path.Pin = TEXT("Paths");
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

	const UE::Geometry::FDynamicMesh3* ResultMesh(const FPCGDataCollection& Output)
	{
		const TArray<FPCGTaggedData> Values = Output.GetInputsByPin(TEXT("Out"));
		if (Values.Num() != 1) { return nullptr; }
		const UPCGDynamicMeshSelectionData* Selection = Cast<UPCGDynamicMeshSelectionData>(Values[0].Data);
		const UPCGDynamicMeshData* Data = Selection ? Selection->GetSourceMeshData() : Cast<UPCGDynamicMeshData>(Values[0].Data);
		return Data ? Data->GetDynamicMesh()->GetMeshPtr() : nullptr;
	}

	int32 CountVerticesAt(const UE::Geometry::FDynamicMesh3& Mesh, const FVector3d& Position)
	{
		int32 Count = 0;
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			Count += Mesh.GetVertex(VertexID).Equals(Position, 1.e-4) ? 1 : 0;
		}
		return Count;
	}

	int32 CountOpenEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.EdgeIndicesItr()) { Count += Mesh.IsBoundaryEdge(EdgeID) ? 1 : 0; }
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshExtrudeToPathsTest,
	"PCGUtils.DynMesh.ExtrudeToPaths.TheRimGoesWhereThePathPointsAre", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshExtrudeToPathsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	const UPCGDynamicMeshSelectionData* Selection = TopOfBox();
	const UE::Geometry::FDynamicMesh3& Before = Selection->GetSourceMeshData()->GetDynamicMesh()->GetMeshRef();

	UPCGDynamicMeshSelectionToPathsSettings* ToPaths = NewObject<UPCGDynamicMeshSelectionToPathsSettings>();
	ToPaths->bOutputToWorldSpace = false;
	ToPaths->bOutputVertexIndex = true;
	const TArray<FPCGTaggedData> Paths = Run(ToPaths, Selection);
	UTEST_EQUAL(TEXT("The top of the box gives one path"), Paths.Num(), 1);

	// What a path node would do: pull the rim halfway in and 5 down. The points are also reversed, since a point is
	// matched by the vertex it names and not by its place in the path.
	UPCGPointArrayData* Moved = CastChecked<UPCGPointArrayData>(Paths[0].Data->DuplicateData(nullptr));
	const int32 NumRim = Moved->GetNumPoints();
	UTEST_EQUAL(TEXT("The rim has 14 vertices"), NumRim, 14);
	TArray<FVector> RimBefore;
	{
		auto Transforms = Moved->GetTransformValueRange();
		auto Entries = Moved->GetMetadataEntryValueRange();
		for (int32 Index = 0; Index < NumRim; ++Index) { RimBefore.Add(Transforms[Index].GetLocation()); }
		for (int32 Index = 0; Index < NumRim / 2; ++Index)
		{
			Swap(Transforms[Index], Transforms[NumRim - 1 - Index]);
			Swap(Entries[Index], Entries[NumRim - 1 - Index]);
		}
		for (int32 Index = 0; Index < NumRim; ++Index)
		{
			const FVector Location = Transforms[Index].GetLocation();
			Transforms[Index].SetLocation(FVector(Location.X * 0.5, Location.Y * 0.5, Location.Z - 5.0));
		}
	}

	UPCGDynMeshExtrudeToPathsSettings* Extrude = NewObject<UPCGDynMeshExtrudeToPathsSettings>();
	Extrude->bPathsInWorldSpace = false;
	Extrude->Matching = EPCGUtilsDynMeshExtrudeToPathsMatching::ByVertexID;
	const UE::Geometry::FDynamicMesh3* After = ResultMesh(RunExtrude(Extrude, Selection, Moved));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh"), After);

	TestEqual(TEXT("The rim is doubled: one new vertex per rim vertex"), After->VertexCount(), Before.VertexCount() + NumRim);
	TestEqual(TEXT("A strip of two faces per rim edge joins the two rims"), After->TriangleCount(), Before.TriangleCount() + 2 * NumRim);
	TestEqual(TEXT("The mesh is still closed"), CountOpenEdges(*After), 0);
	int32 RimLeftBehind = 0, RimAtTarget = 0;
	for (const FVector& Position : RimBefore)
	{
		RimLeftBehind += CountVerticesAt(*After, Position) == 1 ? 1 : 0;
		RimAtTarget += CountVerticesAt(*After, FVector3d(Position.X * 0.5, Position.Y * 0.5, Position.Z - 5.0)) == 1 ? 1 : 0;
	}
	TestEqual(TEXT("The surrounding faces keep their rim where it was"), RimLeftBehind, NumRim);
	TestEqual(TEXT("The selected faces' rim is where the path points are"), RimAtTarget, NumRim);
	// The top is a 5 x 4 grid of vertices: 6 of them are inside the rim.
	TestEqual(TEXT("Keep In Place leaves the vertices inside the rim at the top"), CountVerticesAt(*After, FVector3d(0, 20, 10)) + CountVerticesAt(*After, FVector3d(0, -20, 10)), 2);

	Extrude->InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::FollowRim;
	const UE::Geometry::FDynamicMesh3* Followed = ResultMesh(RunExtrude(Extrude, Selection, Moved));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh when the inside follows"), Followed);
	// The rim moved in evenly from every side, so on average it only went down.
	TestEqual(TEXT("Follow The Rim carries the inside vertices down with it"), CountVerticesAt(*Followed, FVector3d(0, 20, 5)) + CountVerticesAt(*Followed, FVector3d(0, -20, 5)), 2);
	TestEqual(TEXT("And leaves none of them at the top"), CountVerticesAt(*Followed, FVector3d(0, 20, 10)), 0);

	// A path that names only some of the rim moves those and leaves the rest, with no failure.
	UPCGPointArrayData* Half = CastChecked<UPCGPointArrayData>(Moved->DuplicateData(nullptr));
	Half->SetNumPoints(NumRim / 2);
	Extrude->InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;
	AddExpectedError(TEXT("rim vertices are named by no path point"), EAutomationExpectedErrorFlags::Contains, 1);
	const UE::Geometry::FDynamicMesh3* Partial = ResultMesh(RunExtrude(Extrude, Selection, Half));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh for a partial path"), Partial);
	TestEqual(TEXT("The rim is still doubled all the way round"), Partial->VertexCount(), Before.VertexCount() + NumRim);
	int32 Unmoved = 0;
	for (const FVector& Position : RimBefore) { Unmoved += CountVerticesAt(*Partial, Position) == 2 ? 1 : 0; }
	TestEqual(TEXT("The rim vertices with no point stayed, on both sides of the strip"), Unmoved, NumRim - NumRim / 2);
	return true;
}

namespace PCGUtilsDynMeshSelectionToPathsTests
{
	/** A path with no attributes at all: just positions, mesh-local. */
	UPCGPointArrayData* Outline(const TArray<FVector>& Positions)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Positions.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::MetadataEntry);
		auto Transforms = Points->GetTransformValueRange();
		auto Entries = Points->GetMetadataEntryValueRange();
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Positions[Index]);
			Entries[Index] = PCGInvalidEntryKey;
		}
		return Points;
	}

	/** A star round the middle of the box top: Tips * 2 points alternating between two radii, starting at angle Phase. */
	TArray<FVector> Star(int32 Tips, double Outer, double Inner, double Z, double Phase = 0.0)
	{
		TArray<FVector> Positions;
		for (int32 Index = 0; Index < Tips * 2; ++Index)
		{
			const double Angle = Phase + UE_DOUBLE_PI * Index / Tips;
			const double Radius = Index % 2 == 0 ? Outer : Inner;
			Positions.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Z));
		}
		return Positions;
	}

	/** Edges whose two faces run along them the same way: the faces either side disagree about which way is out. */
	int32 CountMisorientedEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			const UE::Geometry::FIndex2i Triangles = Mesh.GetEdgeT(EdgeID);
			if (Triangles.B < 0) { continue; }
			const UE::Geometry::FIndex2i Vertices = Mesh.GetEdgeV(EdgeID);
			auto Runs = [&Mesh, &Vertices](int32 TriangleID)
			{
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				return Triangle[(Triangle.IndexOf(Vertices.A) + 1) % 3] == Vertices.B;
			};
			Count += Runs(Triangles.A) == Runs(Triangles.B) ? 1 : 0;
		}
		return Count;
	}

	int32 CountMissing(const UE::Geometry::FDynamicMesh3& Mesh, const TArray<FVector>& Positions)
	{
		int32 Missing = 0;
		for (const FVector& Position : Positions) { Missing += CountVerticesAt(Mesh, Position) == 1 ? 0 : 1; }
		return Missing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshExtrudeToPathsShapeKeepTest,
	"PCGUtils.DynMesh.ExtrudeToPaths.ByShape.KeepFacesFitsTheRimToAnyOutline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshExtrudeToPathsShapeKeepTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	const UPCGDynamicMeshSelectionData* Selection = TopOfBox();
	const UE::Geometry::FDynamicMesh3& Before = Selection->GetSourceMeshData()->GetDynamicMesh()->GetMeshRef();
	constexpr int32 NumRim = 14;

	UPCGDynMeshExtrudeToPathsSettings* Extrude = NewObject<UPCGDynMeshExtrudeToPathsSettings>();
	Extrude->bPathsInWorldSpace = false;
	TestTrue(TEXT("A new node matches By Shape"), Extrude->Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape);

	// Fewer corners than rim vertices: an inset rectangle with only its 4 corners, wound the other way from the rim
	// and starting somewhere else. No attribute names anything.
	const TArray<FVector> Rectangle = { FVector(80, -40, 10), FVector(-80, -40, 10), FVector(-80, 40, 10), FVector(80, 40, 10) };
	const UE::Geometry::FDynamicMesh3* Inset = ResultMesh(RunExtrude(Extrude, Selection, Outline(Rectangle)));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh for a 4-point outline"), Inset);
	TestEqual(TEXT("4 points: the mesh is still closed"), CountOpenEdges(*Inset), 0);
	TestEqual(TEXT("4 points: every face agrees which way is out"), CountMisorientedEdges(*Inset), 0);
	TestEqual(TEXT("4 points: every corner of the outline is a vertex"), CountMissing(*Inset, Rectangle), 0);
	TestEqual(TEXT("4 points: the rim keeps all its vertices, so one new vertex per rim vertex"), Inset->VertexCount(), Before.VertexCount() + NumRim);
	int32 Stray = 0;
	for (const int32 VertexID : Inset->VertexIndicesItr())
	{
		const FVector3d P = Inset->GetVertex(VertexID);
		if (!FMath::IsNearlyEqual(P.Z, 10.0, 1.e-3)) { continue; }
		const bool bOnRectangle =
			(FMath::IsNearlyEqual(FMath::Abs(P.X), 80.0, 1.e-3) && FMath::Abs(P.Y) <= 40.0 + 1.e-3) ||
			(FMath::IsNearlyEqual(FMath::Abs(P.Y), 40.0, 1.e-3) && FMath::Abs(P.X) <= 80.0 + 1.e-3);
		const bool bOldRim = FMath::Abs(P.X) > 99.0 || FMath::Abs(P.Y) > 59.0;
		const bool bInside = FMath::Abs(P.X) < 51.0 && FMath::Abs(P.Y) < 21.0;
		Stray += bOnRectangle || bOldRim || bInside ? 0 : 1;
	}
	TestEqual(TEXT("4 points: every moved rim vertex lies on the outline"), Stray, 0);

	// More corners than rim vertices: a 24-point star, raised, with the inside following.
	const TArray<FVector> Raised = Star(12, 55, 40, 30, 0.1);
	Extrude->InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::FollowRim;
	const UE::Geometry::FDynamicMesh3* Extruded = ResultMesh(RunExtrude(Extrude, Selection, Outline(Raised)));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh for a 24-point outline"), Extruded);
	TestEqual(TEXT("24 points: the mesh is still closed"), CountOpenEdges(*Extruded), 0);
	TestEqual(TEXT("24 points: every face agrees which way is out"), CountMisorientedEdges(*Extruded), 0);
	TestEqual(TEXT("24 points: every corner of the outline is a vertex"), CountMissing(*Extruded, Raised), 0);
	TestTrue(TEXT("24 points: the corners no rim vertex took were inserted"), Extruded->VertexCount() >= Before.VertexCount() + 24 && Extruded->VertexCount() <= Before.VertexCount() + NumRim + 24);
	TestEqual(TEXT("24 points: Follow The Rim lifted the inside"), CountVerticesAt(*Extruded, FVector3d(0, 20, 10)), 0);

	// The old round trip, with no vertex index: Selection To Paths, pulled in and down, points reversed.
	UPCGDynamicMeshSelectionToPathsSettings* ToPaths = NewObject<UPCGDynamicMeshSelectionToPathsSettings>();
	ToPaths->bOutputToWorldSpace = false;
	const TArray<FPCGTaggedData> Paths = Run(ToPaths, Selection);
	UTEST_EQUAL(TEXT("The top of the box gives one path"), Paths.Num(), 1);
	const UPCGBasePointData* Path = CastChecked<UPCGBasePointData>(Paths[0].Data);
	TArray<FVector> Moved;
	for (int32 Index = Path->GetNumPoints() - 1; Index >= 0; --Index)
	{
		const FVector Location = Path->GetConstTransformValueRange()[Index].GetLocation();
		Moved.Add(FVector(Location.X * 0.5, Location.Y * 0.5, Location.Z - 5.0));
	}
	Extrude->InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;
	const UE::Geometry::FDynamicMesh3* RoundTrip = ResultMesh(RunExtrude(Extrude, Selection, Outline(Moved)));
	UTEST_NOT_NULL(TEXT("The node outputs a mesh for the round trip"), RoundTrip);
	TestEqual(TEXT("Round trip: the mesh is still closed"), CountOpenEdges(*RoundTrip), 0);
	TestEqual(TEXT("Round trip: every face agrees which way is out"), CountMisorientedEdges(*RoundTrip), 0);
	TestEqual(TEXT("Round trip: the rim is where the path points are"), CountMissing(*RoundTrip, Moved), 0);
	TestEqual(TEXT("Round trip: one point per rim vertex gives one new vertex per rim vertex"), RoundTrip->VertexCount(), Before.VertexCount() + NumRim);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshExtrudeToPathsShapeRefillTest,
	"PCGUtils.DynMesh.ExtrudeToPaths.ByShape.RefillReplacesTheFacesWithTheOutline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshExtrudeToPathsShapeRefillTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	const UPCGDynamicMeshSelectionData* Selection = TopOfBox();
	const UE::Geometry::FDynamicMesh3& Before = Selection->GetSourceMeshData()->GetDynamicMesh()->GetMeshRef();
	constexpr int32 NumRim = 14;
	constexpr int32 NumInside = 6;
	// The top is a 5 x 4 grid of vertices: 4 x 3 quads.
	constexpr int32 NumTopBefore = 2 * 4 * 3;

	UPCGDynMeshExtrudeToPathsSettings* Extrude = NewObject<UPCGDynMeshExtrudeToPathsSettings>();
	Extrude->bPathsInWorldSpace = false;
	Extrude->InnerFaces = EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Refill;

	for (const int32 Tips : { 5, 9 })
	{
		const TArray<FVector> Shape = Star(Tips, 55, 25, 10, 0.3);
		const FString Label = FString::Printf(TEXT("%d-point star"), Shape.Num());
		const UE::Geometry::FDynamicMesh3* After = ResultMesh(RunExtrude(Extrude, Selection, Outline(Shape)));
		UTEST_NOT_NULL(*(Label + TEXT(": the node outputs a mesh")), After);
		TestEqual(*(Label + TEXT(": the mesh is still closed")), CountOpenEdges(*After), 0);
		TestEqual(*(Label + TEXT(": every face agrees which way is out")), CountMisorientedEdges(*After), 0);
		TestEqual(*(Label + TEXT(": every corner of the outline is a vertex")), CountMissing(*After, Shape), 0);
		TestEqual(*(Label + TEXT(": the inside vertices are gone and the outline's corners added")), After->VertexCount(), Before.VertexCount() - NumInside + Shape.Num());
		TestEqual(*(Label + TEXT(": no old inside vertex is left")), CountVerticesAt(*After, FVector3d(0, 20, 10)) + CountVerticesAt(*After, FVector3d(50, -20, 10)), 0);
		// An N-gon cap has N - 2 faces; the strip has one face per rim edge plus one per outline corner.
		TestEqual(*(Label + TEXT(": a cap of N - 2 faces and a strip of rim + outline faces")), After->TriangleCount(),
			Before.TriangleCount() - NumTopBefore + (Shape.Num() - 2) + (NumRim + Shape.Num()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshRimToPathPairingTest,
	"PCGUtils.DynMesh.ExtrudeToPaths.ByShape.PairingWalksBothLoopsTheSameWay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshRimToPathPairingTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRimToPath;

	// A square rim of 8 and a 20-point circle inside it, wound the other way.
	const TArray<FVector3d> Rim = { {100, 0, 0}, {100, 100, 0}, {0, 100, 0}, {-100, 100, 0}, {-100, 0, 0}, {-100, -100, 0}, {0, -100, 0}, {100, -100, 0} };
	TArray<FVector3d> Circle;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const double Angle = -2.0 * UE_DOUBLE_PI * Index / 20 + 1.0;
		Circle.Add(FVector3d(60 * FMath::Cos(Angle), 60 * FMath::Sin(Angle), 0));
	}
	AlignTargetToRim(Rim, Circle);
	TestTrue(TEXT("The circle is turned to wind like the rim"), FVector3d::DotProduct(LoopNormal(Rim), LoopNormal(Circle)) > 0.0);
	TestTrue(TEXT("And starts at its point nearest the rim's first"), FVector3d::Distance(Circle[0], FVector3d(60, 0, 0)) < 20.0);

	TArray<int32> Corner;
	PairRimWithCorners(Rim, Circle, Corner);
	UTEST_EQUAL(TEXT("One corner per rim vertex"), Corner.Num(), Rim.Num());
	TestEqual(TEXT("The first rim vertex takes the first corner"), Corner[0], 0);
	bool bRising = true;
	for (int32 Index = 1; Index < Corner.Num(); ++Index) { bRising &= Corner[Index] > Corner[Index - 1]; }
	TestTrue(TEXT("With more corners than rim vertices, every rim vertex has its own corner, in order"), bRising);

	FInnerRimPlan Plan;
	PlanInnerRim(Rim, Circle, Corner, Plan);
	int32 Inserted = 0;
	for (const TArray<int32>& Between : Plan.CornersAfter) { Inserted += Between.Num(); }
	TestEqual(TEXT("The other corners are all inserted between them"), Inserted, Circle.Num() - Rim.Num());

	// The other way round: 8 rim vertices onto a triangle. They share corners, and are spread so none coincide.
	TArray<FVector3d> Triangle = { {80, 0, 0}, {-40, 70, 0}, {-40, -70, 0} };
	AlignTargetToRim(Rim, Triangle);
	PairRimWithCorners(Rim, Triangle, Corner);
	bool bNeverBack = Corner[0] == 0;
	for (int32 Index = 1; Index < Corner.Num(); ++Index) { bNeverBack &= Corner[Index] >= Corner[Index - 1]; }
	TestTrue(TEXT("Fewer corners: the pairing still never goes backwards"), bNeverBack);
	PlanInnerRim(Rim, Triangle, Corner, Plan);
	int32 Coincident = 0;
	for (int32 Index = 0; Index < Plan.RimPositions.Num(); ++Index)
	{
		Coincident += FVector3d::Distance(Plan.RimPositions[Index], Plan.RimPositions[(Index + 1) % Plan.RimPositions.Num()]) < 1.0 ? 1 : 0;
	}
	TestEqual(TEXT("Fewer corners: no two rim vertices land in the same place"), Coincident, 0);
	int32 CornersTaken = 0;
	for (const FVector3d& Point : Triangle)
	{
		for (const FVector3d& Position : Plan.RimPositions) { CornersTaken += Position.Equals(Point, 1.e-6) ? 1 : 0; }
	}
	TestEqual(TEXT("Fewer corners: each corner is still sat on by one rim vertex"), CornersTaken, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshExtrudeToPathsShapeHoleTest,
	"PCGUtils.DynMesh.ExtrudeToPaths.ByShape.EachRimLoopTakesItsOwnPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshExtrudeToPathsShapeHoleTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSelectionToPathsTests;

	// The box top without its middle: one area with an outer rim of 14 and a hole rim of 6.
	UPCGDynamicMeshSelectionData* Full = TopOfBox();
	const UE::Geometry::FDynamicMesh3& Before = Full->GetSourceMeshData()->GetDynamicMesh()->GetMeshRef();
	UE::Geometry::FGeometrySelection Ring;
	Ring.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
	for (const int32 TriangleID : Before.TriangleIndicesItr())
	{
		const FVector3d Centroid = Before.GetTriCentroid(TriangleID);
		const bool bTop = Before.GetTriNormal(TriangleID).Z > 0.9;
		const bool bMiddle = FMath::Abs(Centroid.X) < 50.0 && FMath::Abs(Centroid.Y) < 20.0;
		if (bTop && !bMiddle) { Ring.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded()); }
	}
	UPCGDynamicMeshSelectionData* Selection = NewObject<UPCGDynamicMeshSelectionData>();
	Selection->Initialize(const_cast<UPCGDynamicMeshData*>(Full->GetSourceMeshData()), MoveTemp(Ring));

	// Given hole first, so matching cannot lean on order: the hole grown out, the outside pulled in.
	const TArray<FVector> Hole = { FVector(-60, -28, 10), FVector(0, -28, 10), FVector(60, -28, 10), FVector(60, 0, 10),
		FVector(60, 28, 10), FVector(0, 28, 10), FVector(-60, 28, 10), FVector(-60, 0, 10) };
	const TArray<FVector> Outside = { FVector(85, 50, 10), FVector(-85, 50, 10), FVector(-85, -50, 10), FVector(85, -50, 10) };

	UPCGDynMeshExtrudeToPathsSettings* Extrude = NewObject<UPCGDynMeshExtrudeToPathsSettings>();
	Extrude->bPathsInWorldSpace = false;
	for (const EPCGUtilsDynMeshExtrudeToPathsInnerFaces Mode : { EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Keep, EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Refill })
	{
		Extrude->InnerFaces = Mode;
		const FString Label = Mode == EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Keep ? TEXT("Keep Faces") : TEXT("Refill");
		FPCGDataCollection Input;
		FPCGTaggedData& Mesh = Input.TaggedData.Emplace_GetRef();
		Mesh.Data = Selection;
		Mesh.Pin = TEXT("In");
		for (const TArray<FVector>* Positions : { &Hole, &Outside })
		{
			FPCGTaggedData& Path = Input.TaggedData.Emplace_GetRef();
			Path.Data = Outline(*Positions);
			Path.Pin = TEXT("Paths");
		}
		Input.TaggedData.Emplace_GetRef().Data = Extrude;
		FPCGElementPtr Element = Extrude->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		bool bDone = false;
		for (int32 Iteration = 0; Iteration < 10 && !bDone; ++Iteration) { bDone = Element->Execute(Context.Get()); }
		const UE::Geometry::FDynamicMesh3* After = ResultMesh(Context->OutputData);
		UTEST_NOT_NULL(*(Label + TEXT(": the node outputs a mesh")), After);
		TestEqual(*(Label + TEXT(": the mesh is still closed")), CountOpenEdges(*After), 0);
		TestEqual(*(Label + TEXT(": every face agrees which way is out")), CountMisorientedEdges(*After), 0);
		TestEqual(*(Label + TEXT(": the hole's path is on the mesh")), CountMissing(*After, Hole), 0);
		TestEqual(*(Label + TEXT(": the outside's path is on the mesh")), CountMissing(*After, Outside), 0);
	}
	return true;
}

#endif
