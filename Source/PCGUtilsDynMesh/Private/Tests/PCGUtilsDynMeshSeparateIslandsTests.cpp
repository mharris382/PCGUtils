// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Topology/PCGDynMeshSeparateIslands.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshSeparateIslandsTests
{
	using namespace UE::Geometry;

	int32 AddTriangle(FDynamicMesh3& Mesh, const FVector3d& A, const FVector3d& B, const FVector3d& C)
	{
		return Mesh.AppendTriangle(Mesh.AppendVertex(A), Mesh.AppendVertex(B), Mesh.AppendVertex(C));
	}

	int32 CountIslands(const FDynamicMesh3& Mesh, EPCGDynMeshIslandConnectivity Connectivity, double Distance = 0.0)
	{
		TArray<TArray<int32>> Islands;
		PCGDynMeshSeparateIslands::FindIslands(Mesh, Connectivity, Distance, nullptr, Islands);
		return Islands.Num();
	}

	UPCGDynamicMeshData* TwoBoxes()
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		for (const FVector Center : { FVector(0, 0, 0), FVector(500, 0, 0) })
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
				FGeometryScriptPrimitiveOptions(), FTransform(Center), 100.0, 100.0, 100.0, 0, 0, 0,
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<int32>* Attribute = Domain->FindOrCreateAttribute<int32>(TEXT("HoleIndex"), 0, false, false, true);
		Domain->AddEntry();
		Attribute->SetValue(PCGFirstEntryKey, 7);
		return Data;
	}

	int32 ReadDataInt(const UPCGData* Data, FName Name)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<int32>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : INDEX_NONE;
	}

	int32 TriangleCount(const UPCGData* Data)
	{
		const UPCGDynamicMeshData* Mesh = Cast<const UPCGDynamicMeshData>(Data);
		return Mesh && Mesh->GetDynamicMesh() ? Mesh->GetDynamicMesh()->GetMeshRef().TriangleCount() : INDEX_NONE;
	}

	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, const UPCGData* InputData, const FString& Tag)
	{
		FPCGDataCollection Input;
		FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
		Tagged.Pin = Settings->AllInputPinProperties()[0].Label;
		Tagged.Data = InputData;
		Tagged.Tags.Add(Tag);
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	TArray<const FPCGTaggedData*> OnPin(const TArray<FPCGTaggedData>& Outputs, FName Pin)
	{
		TArray<const FPCGTaggedData*> Result;
		for (const FPCGTaggedData& Output : Outputs)
		{
			if (Output.Pin == Pin)
			{
				Result.Add(&Output);
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshSeparateIslandsFindTest, "PCGUtils.DynMesh.SeparateIslands.FindIslands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshSeparateIslandsFindTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSeparateIslandsTests;

	// A bowtie: two triangles meeting at one shared vertex.
	{
		FDynamicMesh3 Mesh;
		const int32 C = Mesh.AppendVertex(FVector3d(0, 0, 0));
		const int32 A1 = Mesh.AppendVertex(FVector3d(1, 0, 0));
		const int32 A2 = Mesh.AppendVertex(FVector3d(1, 1, 0));
		const int32 B1 = Mesh.AppendVertex(FVector3d(-1, 0, 0));
		const int32 B2 = Mesh.AppendVertex(FVector3d(-1, -1, 0));
		Mesh.AppendTriangle(C, A1, A2);
		Mesh.AppendTriangle(C, B1, B2);
		TestEqual(TEXT("A shared vertex joins a bowtie"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedVertices), 1);
		TestEqual(TEXT("Shared Edges splits a bowtie"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedEdges), 2);
	}

	// Two triangles touching at a point but never welded.
	{
		FDynamicMesh3 Mesh;
		AddTriangle(Mesh, FVector3d(0, 0, 0), FVector3d(1, 0, 0), FVector3d(0, 1, 0));
		AddTriangle(Mesh, FVector3d(0, 0, 0.001), FVector3d(-1, 0, 0), FVector3d(0, -1, 0));
		TestEqual(TEXT("Unwelded parts are separate by vertices"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedVertices), 2);
		TestEqual(TEXT("Proximity joins parts within the distance"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::Proximity, 0.01), 1);
		TestEqual(TEXT("Proximity keeps parts beyond the distance apart"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::Proximity, 0.0001), 2);
	}

	// A trunk (box) with a leaf (triangle) pushed into its side, and a second leaf floating clear of it.
	{
		FDynamicMesh3 Mesh;
		const FVector3d Corners[8] = {
			{-10, -10, 0}, {10, -10, 0}, {10, 10, 0}, {-10, 10, 0},
			{-10, -10, 100}, {10, -10, 100}, {10, 10, 100}, {-10, 10, 100} };
		int32 V[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			V[Index] = Mesh.AppendVertex(Corners[Index]);
		}
		const int32 Faces[6][4] = { {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7} };
		for (const auto& Face : Faces)
		{
			Mesh.AppendTriangle(V[Face[0]], V[Face[1]], V[Face[2]]);
			Mesh.AppendTriangle(V[Face[0]], V[Face[2]], V[Face[3]]);
		}
		// Crosses the +X face (x = 10) from inside the trunk out to x = 60.
		AddTriangle(Mesh, FVector3d(0, 0, 50), FVector3d(60, -20, 50), FVector3d(60, 20, 55));
		// Clear of the trunk.
		AddTriangle(Mesh, FVector3d(200, 0, 50), FVector3d(260, -20, 50), FVector3d(260, 20, 55));

		TestEqual(TEXT("Shared vertices keeps trunk and leaves apart"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedVertices), 3);
		TestEqual(TEXT("Intersecting joins the leaf crossing the trunk, not the clear one"),
			CountIslands(Mesh, EPCGDynMeshIslandConnectivity::Intersecting), 2);

		// With a selection of only the leaves, the crossing runs through the unselected trunk, so nothing joins.
		const TArray<int32> Leaves = { 12, 13 };
		TArray<TArray<int32>> Islands;
		PCGDynMeshSeparateIslands::FindIslands(Mesh, EPCGDynMeshIslandConnectivity::Intersecting, 0.0, &Leaves, Islands);
		TestEqual(TEXT("Intersecting only joins selected triangles"), Islands.Num(), 2);
	}

	// A strip of six triangles: the first (0) and fifth (4) share no vertex.
	{
		FDynamicMesh3 Mesh;
		int32 Bottom[4], Top[4];
		for (int32 Column = 0; Column < 4; ++Column)
		{
			Bottom[Column] = Mesh.AppendVertex(FVector3d(Column, 0, 0));
			Top[Column] = Mesh.AppendVertex(FVector3d(Column, 1, 0));
		}
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Mesh.AppendTriangle(Bottom[Column], Bottom[Column + 1], Top[Column]);
			Mesh.AppendTriangle(Bottom[Column + 1], Top[Column + 1], Top[Column]);
		}
		TestEqual(TEXT("A strip is one island"), CountIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedEdges), 1);
		const TArray<int32> Ends = { 4, 0 };
		TArray<TArray<int32>> Islands;
		PCGDynMeshSeparateIslands::FindIslands(Mesh, EPCGDynMeshIslandConnectivity::SharedVertices, 0.0, &Ends, Islands);
		UTEST_EQUAL(TEXT("Filtered triangles connect only through each other"), Islands.Num(), 2);
		TestEqual(TEXT("Islands are ordered by lowest triangle ID"), Islands[0][0], 0);
		TestEqual(TEXT("The second island is the other end"), Islands[1][0], 4);
	}

	// Extraction keeps polygroup IDs and weight values.
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableTriangleGroups();
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumWeightLayers(1);
		Mesh.Attributes()->GetWeightLayer(0)->SetName(TEXT("Wetness"));
		const int32 First = AddTriangle(Mesh, FVector3d(0, 0, 0), FVector3d(1, 0, 0), FVector3d(0, 1, 0));
		const int32 Second = AddTriangle(Mesh, FVector3d(5, 0, 0), FVector3d(6, 0, 0), FVector3d(5, 1, 0));
		Mesh.SetTriangleGroup(First, 5);
		Mesh.SetTriangleGroup(Second, 9);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			Mesh.Attributes()->GetWeightLayer(0)->SetScalarValue(VertexID, VertexID < 3 ? 0.25f : 0.75f);
		}

		const FDynamicMesh3 Island = PCGDynMeshSeparateIslands::ExtractTriangles(Mesh, { Second });
		UTEST_EQUAL(TEXT("The island has one triangle"), Island.TriangleCount(), 1);
		const int32 IslandTriangle = *Island.TriangleIndicesItr().begin();
		TestEqual(TEXT("The polygroup ID is the source's, not renumbered"), Island.GetTriangleGroup(IslandTriangle), 9);
		UTEST_TRUE(TEXT("The weight layer survives"), Island.HasAttributes() && Island.Attributes()->NumWeightLayers() == 1);
		TestEqual(TEXT("The weight layer keeps its name"), Island.Attributes()->GetWeightLayer(0)->GetName(), FName(TEXT("Wetness")));
		float Value = 0.0f;
		Island.Attributes()->GetWeightLayer(0)->GetValue(Island.GetTriangle(IslandTriangle).A, &Value);
		TestEqual(TEXT("The weight values are the source's"), Value, 0.75f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshSeparateIslandsElementTest, "PCGUtils.DynMesh.SeparateIslands.Element",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshSeparateIslandsElementTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSeparateIslandsTests;
	const FName IslandsPin = PCGDynMeshSeparateIslandsConstants::IslandsPin;
	const FName UnselectedPin = PCGDynMeshSeparateIslandsConstants::UnselectedPin;

	UPCGDynamicMeshData* Boxes = TwoBoxes();
	const int32 BoxTriangles = TriangleCount(Boxes) / 2;

	// Whole mesh: one output per box, each keeping the input's @Data and tags, numbered.
	{
		UPCGDynMeshSeparateIslandsSettings* Settings = NewObject<UPCGDynMeshSeparateIslandsSettings>();
		Settings->bWriteIslandCount = true;
		const TArray<FPCGTaggedData> Outputs = Run(Settings, Boxes, TEXT("Hole3"));
		const TArray<const FPCGTaggedData*> Islands = OnPin(Outputs, IslandsPin);
		UTEST_EQUAL(TEXT("Two boxes give two islands"), Islands.Num(), 2);
		TestEqual(TEXT("Nothing is unselected without a selection"), OnPin(Outputs, UnselectedPin).Num(), 0);
		for (int32 Index = 0; Index < Islands.Num(); ++Index)
		{
			TestEqual(TEXT("Each island is one box"), TriangleCount(Islands[Index]->Data), BoxTriangles);
			TestEqual(TEXT("Each island keeps the input's @Data attribute"), ReadDataInt(Islands[Index]->Data, TEXT("HoleIndex")), 7);
			TestTrue(TEXT("Each island keeps the input's tags"), Islands[Index]->Tags.Contains(TEXT("Hole3")));
			TestEqual(TEXT("Each island is numbered"), ReadDataInt(Islands[Index]->Data, TEXT("IslandIndex")), Index);
			TestEqual(TEXT("Each island knows the count"), ReadDataInt(Islands[Index]->Data, TEXT("IslandCount")), 2);
		}
		TestEqual(TEXT("The input mesh is untouched"), TriangleCount(Boxes), BoxTriangles * 2);
	}

	// A selection of the first box: it is split, and the second box goes to Unselected.
	{
		UE::Geometry::FGeometrySelection Selection;
		Selection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
		for (int32 TriangleID = 0; TriangleID < BoxTriangles; ++TriangleID)
		{
			Selection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
		UPCGDynamicMeshSelectionData* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
		SelectionData->Initialize(Boxes, MoveTemp(Selection));

		UPCGDynMeshSeparateIslandsSettings* Settings = NewObject<UPCGDynMeshSeparateIslandsSettings>();
		const TArray<FPCGTaggedData> Outputs = Run(Settings, SelectionData, TEXT("Hole3"));
		const TArray<const FPCGTaggedData*> Islands = OnPin(Outputs, IslandsPin);
		const TArray<const FPCGTaggedData*> Unselected = OnPin(Outputs, UnselectedPin);
		UTEST_EQUAL(TEXT("Only the selected box is split"), Islands.Num(), 1);
		UTEST_EQUAL(TEXT("The rest is one Unselected mesh"), Unselected.Num(), 1);
		TestEqual(TEXT("The island is the selected box"), TriangleCount(Islands[0]->Data), BoxTriangles);
		TestEqual(TEXT("Unselected is the other box"), TriangleCount(Unselected[0]->Data), BoxTriangles);
		TestEqual(TEXT("Unselected keeps the input's @Data attribute"), ReadDataInt(Unselected[0]->Data, TEXT("HoleIndex")), 7);
		TestTrue(TEXT("Unselected keeps the input's tags"), Unselected[0]->Tags.Contains(TEXT("Hole3")));
	}

	// An invalid output name is reported by name, and nothing is emitted.
	{
		UPCGDynMeshSeparateIslandsSettings* Settings = NewObject<UPCGDynMeshSeparateIslandsSettings>();
		Settings->IslandIndexAttribute = NAME_None;
		AddExpectedError(TEXT("Island Index Attribute is None"), EAutomationExpectedErrorFlags::Contains, 1, false);
		TestEqual(TEXT("A None index name emits nothing"), Run(Settings, Boxes, TEXT("Hole3")).Num(), 0);
	}
	return true;
}

#endif
