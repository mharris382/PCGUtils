// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Topology/PCGDynMeshFillHoles.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "OrientedBoxTypes.h"
#include "PCGContext.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshFillHolesTests
{
	using namespace UE::Geometry;

	/** A 100 cm box, 2x2 quads per face, with its top face (8-edge hole) and optionally its bottom face removed. */
	UPCGDynamicMeshData* MakeOpenBox(bool bOpenBottom)
	{
		FGridBoxMeshGenerator Generator;
		Generator.Box = FOrientedBox3d(FVector3d::Zero(), FVector3d(50.0));
		Generator.EdgeVertices = FIndex3i(3, 3, 3);
		FDynamicMesh3 Mesh(&Generator.Generate());
		TArray<int32> Remove;
		for (int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const double Z = Mesh.GetTriCentroid(TriangleID).Z;
			if (Z > 49.0 || (bOpenBottom && Z < -49.0)) { Remove.Add(TriangleID); }
		}
		for (int32 TriangleID : Remove) { Mesh.RemoveTriangle(TriangleID, false, false); }
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	const FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data) { return *Data->GetDynamicMesh()->GetMeshPtr(); }

	int32 BoundaryEdgeCount(const FDynamicMesh3& InMesh)
	{
		int32 Count = 0;
		for (int32 EdgeID : InMesh.EdgeIndicesItr()) { Count += InMesh.IsBoundaryEdge(EdgeID) ? 1 : 0; }
		return Count;
	}

	/** Boundary edges whose midpoint is at the given height, as an edge-domain selection. */
	UPCGDynamicMeshSelectionData* SelectRimAt(UPCGDynamicMeshData* Data, double Z)
	{
		FGeometrySelection Selection;
		Selection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);
		const FDynamicMesh3& Source = Mesh(Data);
		for (int32 EdgeID : Source.EdgeIndicesItr())
		{
			if (!Source.IsBoundaryEdge(EdgeID)) { continue; }
			FVector3d A, B;
			Source.GetEdgeV(EdgeID, A, B);
			if (FMath::IsNearlyEqual((A.Z + B.Z) * 0.5, Z, 0.01))
			{
				Selection.Selection.Add(FGeoSelectionID::MeshEdge(Source.GetTriEdgeIDFromEdgeID(EdgeID)).Encoded());
			}
		}
		auto* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
		SelectionData->Initialize(Data, MoveTemp(Selection));
		return SelectionData;
	}

	FPCGDataCollection Run(UPCGDynMeshFillHolesSettings* Settings, const UPCGData* Input)
	{
		FPCGDataCollection Collection;
		auto& Tagged = Collection.TaggedData.Emplace_GetRef();
		Tagged.Data = Input;
		Tagged.Pin = TEXT("In");
		Collection.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Collection, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData; }
		}
		return {};
	}

	const UPCGDynamicMeshData* OutputMesh(const FPCGDataCollection& Output)
	{
		const auto Values = Output.GetInputsByPin(TEXT("Out"));
		if (Values.Num() != 1) { return nullptr; }
		const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(Values[0].Data);
		return Selection ? Selection->GetSourceMeshData() : Cast<UPCGDynamicMeshData>(Values[0].Data);
	}

	const UPCGDynamicMeshSelectionData* OutputSelection(const FPCGDataCollection& Output)
	{
		const auto Values = Output.GetInputsByPin(TEXT("Out"));
		return Values.Num() == 1 ? Cast<UPCGDynamicMeshSelectionData>(Values[0].Data) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFillHolesAllTypesTest,
	"PCGUtils.DynMesh.FillHoles.EveryFillTypeClosesTheHole", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshFillHolesAllTypesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshFillHolesTests;
	UPCGDynamicMeshData* Source = MakeOpenBox(false);
	if (!TestEqual(TEXT("Fixture has one 8-edge hole"), BoundaryEdgeCount(Mesh(Source)), 8)) { return false; }
	const int32 SourceTriangles = Mesh(Source).TriangleCount();

	const EHoleFillOpFillType Types[] = { EHoleFillOpFillType::TriangleFan, EHoleFillOpFillType::PolygonEarClipping,
		EHoleFillOpFillType::Planar, EHoleFillOpFillType::Minimal, EHoleFillOpFillType::Smooth };
	for (EHoleFillOpFillType Type : Types)
	{
		const FString Name = StaticEnum<EHoleFillOpFillType>()->GetNameStringByValue(static_cast<int64>(Type));
		auto* Settings = NewObject<UPCGDynMeshFillHolesSettings>();
		Settings->FillType = Type;
		Settings->bOutputSelectionData = true;
		const FPCGDataCollection Output = Run(Settings, Source);
		const UPCGDynamicMeshData* Result = OutputMesh(Output);
		const UPCGDynamicMeshSelectionData* Selection = OutputSelection(Output);
		if (!TestNotNull(*(Name + TEXT(": output mesh")), Result) || !TestNotNull(*(Name + TEXT(": result selection")), Selection))
		{
			continue;
		}
		TestEqual(*(Name + TEXT(": mesh is closed")), BoundaryEdgeCount(Mesh(Result)), 0);
		TestTrue(*(Name + TEXT(": triangles were added")), Mesh(Result).TriangleCount() > SourceTriangles);
		TestTrue(*(Name + TEXT(": result selection holds the fill")), Selection->GetSelection().Selection.Num() > 0);
		for (uint64 Encoded : Selection->GetSelection().Selection)
		{
			const int32 TriangleID = UE::Geometry::FGeoSelectionID(Encoded).GeometryID;
			TestTrue(*(Name + TEXT(": fill triangle is at the top")), Mesh(Result).IsTriangle(TriangleID) &&
				Mesh(Result).GetTriCentroid(TriangleID).Z > 40.0);
		}
		TestEqual(*(Name + TEXT(": upstream mesh untouched")), BoundaryEdgeCount(Mesh(Source)), 8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFillHolesSelectionTest,
	"PCGUtils.DynMesh.FillHoles.SelectionAndSizeLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshFillHolesSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshFillHolesTests;
	UPCGDynamicMeshData* Source = MakeOpenBox(true);
	if (!TestEqual(TEXT("Fixture has two 8-edge holes"), BoundaryEdgeCount(Mesh(Source)), 16)) { return false; }

	auto* Settings = NewObject<UPCGDynMeshFillHolesSettings>();
	const UPCGDynamicMeshData* All = OutputMesh(Run(Settings, Source));
	if (TestNotNull(TEXT("Bare mesh output"), All))
	{
		TestEqual(TEXT("Bare mesh fills every hole"), BoundaryEdgeCount(Mesh(All)), 0);
	}

	// One selected rim edge is enough to choose its hole.
	UPCGDynamicMeshSelectionData* TopRim = SelectRimAt(Source, 50.0);
	if (TestEqual(TEXT("Top rim selection has 8 edges"), TopRim->GetSelection().Selection.Num(), 8))
	{
		const UPCGDynamicMeshData* TopOnly = OutputMesh(Run(Settings, TopRim));
		if (TestNotNull(TEXT("Selection output"), TopOnly))
		{
			TestEqual(TEXT("Only the selected hole is filled"), BoundaryEdgeCount(Mesh(TopOnly)), 8);
			for (int32 EdgeID : Mesh(TopOnly).EdgeIndicesItr())
			{
				if (!Mesh(TopOnly).IsBoundaryEdge(EdgeID)) { continue; }
				FVector3d A, B;
				Mesh(TopOnly).GetEdgeV(EdgeID, A, B);
				TestTrue(TEXT("Remaining boundary is the bottom rim"), A.Z < -49.0 && B.Z < -49.0);
			}
		}
	}

	Settings->MaxHoleEdges = 7;
	const UPCGDynamicMeshData* Limited = OutputMesh(Run(Settings, Source));
	if (TestNotNull(TEXT("Size-limited output"), Limited))
	{
		TestEqual(TEXT("Holes over Max Hole Edges stay open"), BoundaryEdgeCount(Mesh(Limited)), 16);
	}
	Settings->MaxHoleEdges = 8;
	const UPCGDynamicMeshData* AtLimit = OutputMesh(Run(Settings, Source));
	if (TestNotNull(TEXT("At-limit output"), AtLimit))
	{
		TestEqual(TEXT("Holes at Max Hole Edges are filled"), BoundaryEdgeCount(Mesh(AtLimit)), 0);
	}
	return true;
}

#endif
