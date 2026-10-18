// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynMeshInsideMeshSelectionFactory.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshInsideMeshSelectionTests
{
	using namespace UE::Geometry;

	/** The volume: a closed box from -50 to 50 on every axis. */
	UPCGDynamicMeshData* MakeVolume()
	{
		FGridBoxMeshGenerator Generator;
		Generator.Box = FOrientedBox3d(FVector3d::Zero(), FVector3d(50.0));
		Generator.EdgeVertices = FIndex3i(2, 2, 2);
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(FDynamicMesh3(&Generator.Generate()));
		return Data;
	}

	/** The tested mesh: a strip crossing the volume's +X face, columns at x = 25, 35, ..., 85 and y = 0, 10. */
	UPCGDynamicMeshData* MakeStrip()
	{
		FDynamicMesh3 Mesh;
		for (int32 Column = 0; Column <= 6; ++Column)
		{
			Mesh.AppendVertex(FVector3d(25.0 + Column * 10.0, 0.0, 0.0));
			Mesh.AppendVertex(FVector3d(25.0 + Column * 10.0, 10.0, 0.0));
		}
		for (int32 Column = 0; Column < 6; ++Column)
		{
			const int32 A = Column * 2, B = A + 1, C = A + 2, D = A + 3;
			Mesh.AppendTriangle(A, C, D);
			Mesh.AppendTriangle(A, D, B);
		}
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	UPCGDynMeshInsideMeshSelectionFactoryData* MakeSelector(const UPCGDynamicMeshData* Volume, double Offset, bool bInvert = false)
	{
		UPCGDynMeshInsideMeshSelectionFactoryData* Selector = NewObject<UPCGDynMeshInsideMeshSelectionFactoryData>();
		if (Volume)
		{
			Selector->VolumeMeshes.Add(Volume);
		}
		Selector->Offset = Offset;
		Selector->bInvertSelection = bInvert;
		// Both test meshes are authored in one space and there is no PCG target actor to resolve.
		Selector->bMeshesAreActorLocal = false;
		return Selector;
	}

	/** Evaluates the selector in the vertex domain and returns the sorted X positions of the selected vertices. */
	bool SelectedX(const UPCGUtilsDynMeshSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data, TArray<double>& OutX)
	{
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = EGeometryElementType::Vertex;
		const FDynamicMesh3& Mesh = *Data->GetDynamicMesh()->GetMeshPtr();
		const FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh, Domain);
		FGeometrySelection Result;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Evaluation, nullptr, Result))
		{
			return false;
		}
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(MoveTemp(Result));
		TArray<int32> IDs;
		ScriptSelection.ConvertToMeshIndexArray(Mesh, IDs, EGeometryScriptIndexType::Vertex);
		OutX.Reset();
		for (const int32 ID : IDs)
		{
			OutX.AddUnique(Mesh.GetVertex(ID).X);
		}
		OutX.Sort();
		return true;
	}

	FString Describe(const TArray<double>& Values)
	{
		return FString::JoinBy(Values, TEXT(", "), [](double Value) { return FString::SanitizeFloat(Value); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshInsideMeshSelectionTest,
	"PCGUtils.DynMesh.Selection.InsideMesh.SelectsByVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshInsideMeshSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshInsideMeshSelectionTests;

	const UPCGDynamicMeshData* Volume = MakeVolume();
	const UPCGDynamicMeshData* Strip = MakeStrip();

	struct FCase
	{
		const TCHAR* Name;
		double Offset;
		bool bInvert;
		TArray<double> Expected;
	};
	const FCase Cases[] = {
		{TEXT("Inside"), 0.0, false, {25.0, 35.0, 45.0}},
		{TEXT("Outside (inverted)"), 0.0, true, {55.0, 65.0, 75.0, 85.0}},
		// Grown by 10: x = 55 is 5 outside the +X face, so it joins.
		{TEXT("Grown by 10"), 10.0, false, {25.0, 35.0, 45.0, 55.0}},
		// Shrunk by 10: x = 45 is only 5 deep, so it leaves.
		{TEXT("Shrunk by 10"), -10.0, false, {25.0, 35.0}},
	};

	for (const FCase& Case : Cases)
	{
		TArray<double> Actual;
		if (!TestTrue(*FString::Printf(TEXT("%s evaluates"), Case.Name), SelectedX(MakeSelector(Volume, Case.Offset, Case.bInvert), Strip, Actual)))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s selects x = %s"), Case.Name, *Describe(Case.Expected)), Describe(Actual), Describe(Case.Expected));
	}

	// No volume: an error that says so, and no selection.
	{
		TArray<double> Actual;
		AddExpectedError(TEXT("needs at least one DynMesh with triangles"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("A selector with no volume fails"), SelectedX(MakeSelector(nullptr, 0.0), Strip, Actual));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
