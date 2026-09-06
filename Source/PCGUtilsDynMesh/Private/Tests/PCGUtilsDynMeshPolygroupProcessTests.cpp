// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Attributes/PCGDynMeshClearPolygroups.h"
#include "Elements/Attributes/PCGDynMeshSetPolygroup.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPolygroupProcessTests
{
	using namespace UE::Geometry;

	/** Two triangles sharing an edge, every triangle assigned InitialGroup. */
	UPCGDynamicMeshData* MakeGroupedQuad(int32 InitialGroup)
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableTriangleGroups(InitialGroup);
		Mesh.AppendVertex(FVector3d(0, 0, 0));
		Mesh.AppendVertex(FVector3d(10, 0, 0));
		Mesh.AppendVertex(FVector3d(10, 10, 0));
		Mesh.AppendVertex(FVector3d(0, 10, 0));
		const int32 T0 = Mesh.AppendTriangle(0, 1, 2);
		const int32 T1 = Mesh.AppendTriangle(0, 2, 3);
		Mesh.SetTriangleGroup(T0, InitialGroup);
		Mesh.SetTriangleGroup(T1, InitialGroup);

		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	UPCGDynamicMeshSelectionData* MakeTriangleSelection(const UPCGDynamicMeshData* MeshData, TArrayView<const int32> TriangleIDs)
	{
		FGeometrySelection Selection;
		Selection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : TriangleIDs)
		{
			Selection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
		UPCGDynamicMeshSelectionData* Data = NewObject<UPCGDynamicMeshSelectionData>();
		Data->Initialize(MeshData, MoveTemp(Selection));
		return Data;
	}

	FPCGDataCollection Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, const UPCGData* Input)
	{
		FPCGDataCollection InputCollection;
		FPCGTaggedData& Tagged = InputCollection.TaggedData.Emplace_GetRef();
		Tagged.Data = Input;
		Tagged.Pin = TEXT("In");
		InputCollection.TaggedData.Emplace_GetRef().Data = Settings;

		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&InputCollection, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData;
			}
		}
		return {};
	}

	const FDynamicMesh3* ResultMesh(const FPCGDataCollection& Output)
	{
		const TArray<FPCGTaggedData> Out = Output.GetInputsByPin(TEXT("Out"));
		const UPCGDynamicMeshData* MeshData = Out.IsValidIndex(0) ? Cast<UPCGDynamicMeshData>(Out[0].Data.Get()) : nullptr;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		return DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshSetPolygroupTest,
	"PCGUtils.DynMesh.PolyGroup.SetPolyGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshSetPolygroupTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPolygroupProcessTests;

	// No selection -> whole mesh gets the ID.
	{
		UPCGDynMeshSetPolygroupSettings* Settings = NewObject<UPCGDynMeshSetPolygroupSettings>();
		Settings->PolygroupID = 5;
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeGroupedQuad(0)));
		if (!TestNotNull(TEXT("Set (no selection) produced a mesh"), Mesh)) { return false; }
		TestEqual(TEXT("Triangle 0 -> PolyGroup 5"), Mesh->GetTriangleGroup(0), 5);
		TestEqual(TEXT("Triangle 1 -> PolyGroup 5"), Mesh->GetTriangleGroup(1), 5);
	}

	// Selection of triangle 0 only -> triangle 1 keeps its original group.
	{
		UPCGDynamicMeshData* MeshData = MakeGroupedQuad(2);
		const int32 SelectedTri = 0;
		UPCGDynMeshSetPolygroupSettings* Settings = NewObject<UPCGDynMeshSetPolygroupSettings>();
		Settings->PolygroupID = 9;
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeTriangleSelection(MeshData, MakeArrayView(&SelectedTri, 1))));
		if (!TestNotNull(TEXT("Set (selection) produced a mesh"), Mesh)) { return false; }
		TestEqual(TEXT("Selected triangle 0 -> PolyGroup 9"), Mesh->GetTriangleGroup(0), 9);
		TestEqual(TEXT("Unselected triangle 1 keeps PolyGroup 2"), Mesh->GetTriangleGroup(1), 2);
	}

	// Generate New PolyGroup -> both triangles share one freshly allocated non-zero ID.
	{
		UPCGDynMeshSetPolygroupSettings* Settings = NewObject<UPCGDynMeshSetPolygroupSettings>();
		Settings->bGenerateNewPolygroup = true;
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeGroupedQuad(3)));
		if (!TestNotNull(TEXT("Set (generate new) produced a mesh"), Mesh)) { return false; }
		const int32 NewID = Mesh->GetTriangleGroup(0);
		TestTrue(TEXT("A fresh non-zero PolyGroup ID was allocated"), NewID != 0 && NewID != 3);
		TestEqual(TEXT("Both triangles share the new ID"), Mesh->GetTriangleGroup(1), NewID);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshClearPolygroupsTest,
	"PCGUtils.DynMesh.PolyGroup.ClearPolyGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshClearPolygroupsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPolygroupProcessTests;

	// Selection of triangle 0 only -> triangle 1 keeps its group; triangle 0 is reset to 0.
	{
		UPCGDynamicMeshData* MeshData = MakeGroupedQuad(7);
		const int32 SelectedTri = 0;
		UPCGDynMeshClearPolygroupsSettings* Settings = NewObject<UPCGDynMeshClearPolygroupsSettings>();
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeTriangleSelection(MeshData, MakeArrayView(&SelectedTri, 1))));
		if (!TestNotNull(TEXT("Clear (selection) produced a mesh"), Mesh)) { return false; }
		TestEqual(TEXT("Selected triangle 0 -> PolyGroup 0"), Mesh->GetTriangleGroup(0), 0);
		TestEqual(TEXT("Unselected triangle 1 keeps PolyGroup 7"), Mesh->GetTriangleGroup(1), 7);
	}

	// No selection -> the whole layer is cleared.
	{
		UPCGDynMeshClearPolygroupsSettings* Settings = NewObject<UPCGDynMeshClearPolygroupsSettings>();
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeGroupedQuad(7)));
		if (!TestNotNull(TEXT("Clear (no selection) produced a mesh"), Mesh)) { return false; }
		TestEqual(TEXT("Triangle 0 -> PolyGroup 0"), Mesh->GetTriangleGroup(0), 0);
		TestEqual(TEXT("Triangle 1 -> PolyGroup 0"), Mesh->GetTriangleGroup(1), 0);
	}

	// Non-zero Clear Value.
	{
		UPCGDynMeshClearPolygroupsSettings* Settings = NewObject<UPCGDynMeshClearPolygroupsSettings>();
		Settings->ClearValue = 4;
		const FDynamicMesh3* Mesh = ResultMesh(Run(Settings, MakeGroupedQuad(7)));
		if (!TestNotNull(TEXT("Clear (value 4) produced a mesh"), Mesh)) { return false; }
		TestEqual(TEXT("Triangle 0 -> Clear Value 4"), Mesh->GetTriangleGroup(0), 4);
		TestEqual(TEXT("Triangle 1 -> Clear Value 4"), Mesh->GetTriangleGroup(1), 4);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
