// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGSelectMeshOcclusion.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Geometry/PCGUtilsDynMeshOcclusion.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshMeshOcclusionTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FDynamicMesh3 MakeBoxMesh(const FVector3d& Center, const FVector3d& Extents)
	{
		FGridBoxMeshGenerator Generator;
		Generator.Box = FOrientedBox3d(Center, Extents);
		Generator.EdgeVertices = FIndex3i(2, 2, 2);
		return FDynamicMesh3(&Generator.Generate());
	}

	/** A strip at height Z facing +Z (or -Z when bFlip), columns at x = 0, 10, ..., 100 and y = 0, 10. */
	void AppendStrip(FDynamicMesh3& Mesh, double Z, bool bFlip = false)
	{
		const int32 First = Mesh.MaxVertexID();
		for (int32 Column = 0; Column <= 10; ++Column)
		{
			Mesh.AppendVertex(FVector3d(Column * 10.0, 0.0, Z));
			Mesh.AppendVertex(FVector3d(Column * 10.0, 10.0, Z));
		}
		for (int32 Column = 0; Column < 10; ++Column)
		{
			const int32 A = First + Column * 2, B = A + 1, C = A + 2, D = A + 3;
			if (bFlip)
			{
				Mesh.AppendTriangle(A, D, C);
				Mesh.AppendTriangle(A, B, D);
			}
			else
			{
				Mesh.AppendTriangle(A, C, D);
				Mesh.AppendTriangle(A, D, B);
			}
		}
	}

	UPCGDynamicMeshData* MakeData(FDynamicMesh3&& Mesh)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	UPCGSelectMeshOcclusionFactoryData* MakeSelector(TArray<const UPCGDynamicMeshData*> Occluders)
	{
		UPCGSelectMeshOcclusionFactoryData* Selector = NewObject<UPCGSelectMeshOcclusionFactoryData>();
		for (const UPCGDynamicMeshData* Occluder : Occluders)
		{
			Selector->OccluderMeshes.Add(Occluder);
		}
		// Test meshes share one space and there is no PCG target actor to resolve.
		Selector->bMeshesAreActorLocal = false;
		return Selector;
	}

	/** Evaluates in the vertex domain and returns the sorted, distinct X positions of selected vertices at height Z. */
	bool SelectedX(const UPCGUtilsDynMeshSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data, double Z, TArray<double>& OutX)
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
			if (FMath::IsNearlyEqual(Mesh.GetVertex(ID).Z, Z))
			{
				OutX.AddUnique(Mesh.GetVertex(ID).X);
			}
		}
		OutX.Sort();
		return true;
	}

	FString Describe(const TArray<double>& Values)
	{
		return FString::JoinBy(Values, TEXT(", "), [](double Value) { return FString::SanitizeFloat(Value); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMeshOcclusionCoreTest, "PCGUtils.DynMesh.Selection.MeshOcclusion.OccluderSet",
	PCGUtilsDynMeshMeshOcclusionTests::Flags)
bool FPCGUtilsMeshOcclusionCoreTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshMeshOcclusionTests;
	// A unit box (-1..1), placed by a rotated, non-uniformly scaled transform: X scale 10, Y 20, Z 5, moved
	// to (100, 0, 0) and turned 90 degrees about Z. In world space it spans x 80..120, y -10..10, z -5..5.
	const FDynamicMesh3 Box = MakeBoxMesh(FVector3d::Zero(), FVector3d(1.0));
	const FTransform Placement(FRotator(0, 90, 0), FVector(100, 0, 0), FVector(10, 20, 5));
	PCGUtilsDynMeshOcclusion::FOccluderSet Set;
	TestEqual(TEXT("Box added"), Set.Add(Box, Placement), 0);
	FDynamicMesh3 Empty;
	TestEqual(TEXT("An empty mesh is not added"), Set.Add(Empty, FTransform::Identity), INDEX_NONE);

	double Distance = 0.0;
	TestTrue(TEXT("A ray along +X hits the box"), Set.FindNearestHit(FVector3d(0, 0, 0), FVector3d(1, 0, 0), 0.0, Distance));
	TestEqual(TEXT("It hits the near face at x = 80, in world units"), Distance, 80.0, 1e-6);
	TestTrue(TEXT("A ray down from above hits the top at z = 5"), Set.FindNearestHit(FVector3d(100, 0, 50), FVector3d(0, 0, -1), 0.0, Distance));
	TestEqual(TEXT("45 units down"), Distance, 45.0, 1e-6);
	TestFalse(TEXT("Max Distance is in world units"), Set.IsOccluded(FVector3d(100, 0, 50), FVector3d(0, 0, -1), 40.0));
	TestTrue(TEXT("Just past the hit is enough"), Set.IsOccluded(FVector3d(100, 0, 50), FVector3d(0, 0, -1), 46.0));
	TestFalse(TEXT("A ray past the box misses"), Set.IsOccluded(FVector3d(100, 15, 50), FVector3d(0, 0, -1), 0.0));
	TestFalse(TEXT("A ray pointing away misses"), Set.IsOccluded(FVector3d(0, 0, 0), FVector3d(-1, 0, 0), 0.0));

	const FVector3d Normal = PCGUtilsDynMeshOcclusion::TransformNormal(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1, 4, 1)),
		FVector3d(1, 1, 0).GetSafeNormal());
	TestTrue(TEXT("Normals use the inverse transpose under non-uniform scale"), Normal.Equals(FVector3d(4, 1, 0).GetSafeNormal(), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMeshOcclusionSelectTest, "PCGUtils.DynMesh.Selection.MeshOcclusion.Selects",
	PCGUtilsDynMeshMeshOcclusionTests::Flags)
bool FPCGUtilsMeshOcclusionSelectTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshMeshOcclusionTests;
	FDynamicMesh3 Ground;
	AppendStrip(Ground, 0.0);
	const UPCGDynamicMeshData* Tested = MakeData(MoveTemp(Ground));
	// Roof over x -5..45 at z 50..60 (clear of the vertex columns at its edges); wall beside x = 100 that only a sideways ray reaches.
	const UPCGDynamicMeshData* Roof = MakeData(MakeBoxMesh(FVector3d(20, 5, 55), FVector3d(25, 20, 5)));
	const UPCGDynamicMeshData* Wall = MakeData(MakeBoxMesh(FVector3d(150, 5, 0), FVector3d(5, 50, 50)));

	TArray<double> Actual;
	UPCGSelectMeshOcclusionFactoryData* Selector = MakeSelector({Roof});
	TestTrue(TEXT("Evaluates"), SelectedX(Selector, Tested, 0.0, Actual));
	TestEqual(TEXT("Vertices under the roof are occluded"), Describe(Actual), Describe({0, 10, 20, 30, 40}));

	Selector->Result = EPCGDynMeshSelfOcclusionResult::Unoccluded;
	TestTrue(TEXT("Evaluates unoccluded"), SelectedX(Selector, Tested, 0.0, Actual));
	TestEqual(TEXT("The rest are clear"), Describe(Actual), Describe({50, 60, 70, 80, 90, 100}));

	Selector->Result = EPCGDynMeshSelfOcclusionResult::Occluded;
	Selector->MaximumDistance = 30.0;
	TestTrue(TEXT("Evaluates with a short ray"), SelectedX(Selector, Tested, 0.0, Actual));
	TestEqual(TEXT("A 30 unit ray does not reach a roof 50 up"), Actual.Num(), 0);

	// Two occluders act together; a sideways ray only meets the wall.
	Selector = MakeSelector({Roof, Wall});
	Selector->TraceDirection = FVector(1, 0, 0);
	TestTrue(TEXT("Evaluates sideways"), SelectedX(Selector, Tested, 0.0, Actual));
	TestEqual(TEXT("Every vertex sees the wall to +X"), Actual.Num(), 11);
	Selector->TraceDirection = FVector(0, 0, 1);
	TestTrue(TEXT("Evaluates up with both"), SelectedX(Selector, Tested, 0.0, Actual));
	TestEqual(TEXT("The wall does not cover anything from above"), Describe(Actual), Describe({0, 10, 20, 30, 40}));

	// Include Self: a second, smaller plate over x 60..100 belongs to the tested mesh itself.
	FDynamicMesh3 TwoLevels;
	AppendStrip(TwoLevels, 0.0);
	{
		const int32 First = TwoLevels.MaxVertexID();
		for (int32 Column = 6; Column <= 10; ++Column)
		{
			TwoLevels.AppendVertex(FVector3d(Column * 10.0 - 5.0, -5.0, 20.0));
			TwoLevels.AppendVertex(FVector3d(Column * 10.0 - 5.0, 15.0, 20.0));
		}
		for (int32 Column = 0; Column < 4; ++Column)
		{
			const int32 A = First + Column * 2, B = A + 1, C = A + 2, D = A + 3;
			TwoLevels.AppendTriangle(A, C, D);
			TwoLevels.AppendTriangle(A, D, B);
		}
	}
	const UPCGDynamicMeshData* SelfShadowed = MakeData(MoveTemp(TwoLevels));
	Selector = MakeSelector({Roof});
	TestTrue(TEXT("Evaluates without self"), SelectedX(Selector, SelfShadowed, 0.0, Actual));
	TestEqual(TEXT("Without Include Self only the roof counts"), Describe(Actual), Describe({0, 10, 20, 30, 40}));
	Selector->bIncludeSelf = true;
	TestTrue(TEXT("Evaluates with self"), SelectedX(Selector, SelfShadowed, 0.0, Actual));
	TestEqual(TEXT("Include Self adds the vertices under the mesh's own upper plate"),
		Describe(Actual), Describe({0, 10, 20, 30, 40, 60, 70, 80, 90}));
	TestTrue(TEXT("Evaluates the upper plate"), SelectedX(Selector, SelfShadowed, 20.0, Actual));
	TestEqual(TEXT("The upper plate does not block itself"), Actual.Num(), 0);

	// No usable occluders: an error saying so.
	FDynamicMesh3 EmptyMesh;
	Selector = MakeSelector({MakeData(MoveTemp(EmptyMesh))});
	AddExpectedError(TEXT("has no triangles and blocks nothing"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("needs at least one DynMesh with triangles on its Occluders pin"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("No usable occluder fails"), SelectedX(Selector, Tested, 0.0, Actual));
	return true;
}

#endif
