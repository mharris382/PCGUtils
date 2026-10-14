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

#endif
