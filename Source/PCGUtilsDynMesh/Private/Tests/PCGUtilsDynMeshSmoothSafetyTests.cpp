// Copyright Max Harris
//
// DynMesh | Smooth never folds or collapses a triangle (Prevent Degenerate Triangles), and locked vertices stay put
// while their neighbours relax against them.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Deform/PCGSmoothDynamicMesh.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshSmoothSafetyTests
{
	using namespace UE::Geometry;

	/**
	 * A fan around one interior vertex (-50, -50) whose ring is an L: the plain average of the ring, (-3.3, -3.3), lies
	 * in the L's notch, outside the ring. One full-strength Laplacian step moves the centre there and folds triangles.
	 */
	UPCGDynamicMeshData* MakeNotchedFan(int32& OutCenter)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->GetMutableDynamicMesh()->EditMesh([&OutCenter](FDynamicMesh3& Mesh)
		{
			Mesh.EnableAttributes();
			OutCenter = Mesh.AppendVertex(FVector3d(-50, -50, 0));
			const FVector3d Ring[] = {
				FVector3d(-100, -100, 0), FVector3d(100, -100, 0), FVector3d(100, -10, 0),
				FVector3d(-10, -10, 0), FVector3d(-10, 100, 0), FVector3d(-100, 100, 0)};
			TArray<int32> RingIDs;
			for (const FVector3d& P : Ring)
			{
				RingIDs.Add(Mesh.AppendVertex(P));
			}
			for (int32 I = 0; I < RingIDs.Num(); ++I)
			{
				const int32 A = RingIDs[I];
				const int32 B = RingIDs[(I + 1) % RingIDs.Num()];
				// GeometryCore's triangle normal is (C - A) x (B - A); wind each triangle to face +Z.
				const FVector3d PC = Mesh.GetVertex(OutCenter);
				const double NormalZ = (Mesh.GetVertex(B) - PC).Cross(Mesh.GetVertex(A) - PC).Z;
				Mesh.AppendTriangle(OutCenter, NormalZ >= 0.0 ? A : B, NormalZ >= 0.0 ? B : A);
			}
		});
		return Data;
	}

	TArray<FPCGTaggedData> Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, const UPCGData* Input)
	{
		FPCGDataCollection Collection;
		FPCGTaggedData& Main = Collection.TaggedData.Emplace_GetRef();
		Main.Pin = Settings->AllInputPinProperties()[0].Label;
		Main.Data = Input;
		Collection.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Collection, nullptr, nullptr)));
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

	/** Triangles facing down (folded over) or with no area. */
	int32 CountBadTriangles(const FDynamicMesh3& Mesh)
	{
		int32 Bad = 0;
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d Normal;
			double Area = 0.0;
			FVector3d Centroid;
			Mesh.GetTriInfo(Tri, Normal, Area, Centroid);
			if (Normal.Z <= 0.0 || Area < 1.0)
			{
				++Bad;
			}
		}
		return Bad;
	}

	const FDynamicMesh3* RunSmooth(FAutomationTestBase& Test, bool bGuard, int32& OutCenter)
	{
		UPCGSmoothDynamicMeshSettings* Settings = NewObject<UPCGSmoothDynamicMeshSettings>();
		Settings->SmoothingMethod = EPCGUtilsDynamicMeshSmoothingMethod::UniformLaplacian;
		Settings->Iterations = 1;
		Settings->Strength = 1.0f;
		Settings->bPreserveBoundaries = true;
		Settings->BoundarySmoothingWeight = 0.0f;
		Settings->bRecomputeNormalsAfterSmoothing = false;
		Settings->bPreventDegenerateTriangles = bGuard;
		const TArray<FPCGTaggedData> Output = Run(Settings, MakeNotchedFan(OutCenter));
		const UPCGDynamicMeshData* Data = Output.Num() == 1 ? Cast<UPCGDynamicMeshData>(Output[0].Data) : nullptr;
		Test.TestNotNull(TEXT("Smooth produced DynMesh data"), Data);
		return Data ? Data->GetDynamicMesh()->GetMeshPtr() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshSmoothSafetyTest, "PCGUtils.DynMesh.Smooth.NoDegenerateTriangles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshSmoothSafetyTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsDynMeshSmoothSafetyTests;

	// Without the guard the step folds the fan: the case this protects against really happens.
	int32 Center = INDEX_NONE;
	if (const FDynamicMesh3* Unguarded = RunSmooth(*this, false, Center))
	{
		TestTrue(TEXT("Unguarded: the centre jumped into the notch"), Unguarded->GetVertex(Center).Equals(FVector3d(-20.0 / 6.0, -20.0 / 6.0, 0.0), 1e-6));
		TestTrue(TEXT("Unguarded: triangles fold"), CountBadTriangles(*Unguarded) > 0);
		// The ring is the boundary and locked: none of it moved.
		TestTrue(TEXT("Unguarded: locked boundary stayed put"), Unguarded->GetVertex(Center + 1).Equals(FVector3d(-100, -100, 0), 1e-9));
	}

	// With the guard (the default) nothing folds or collapses.
	if (const FDynamicMesh3* Guarded = RunSmooth(*this, true, Center))
	{
		TestEqual(TEXT("Guarded: no folded or collapsed triangles"), CountBadTriangles(*Guarded), 0);
		TestTrue(TEXT("Guarded: the centre kept a valid place"), Guarded->GetVertex(Center).Equals(FVector3d(-50, -50, 0), 1e-6));
	}
	TestTrue(TEXT("The guard is on by default"), GetDefault<UPCGSmoothDynamicMeshSettings>()->bPreventDegenerateTriangles);
	return true;
}

#endif
