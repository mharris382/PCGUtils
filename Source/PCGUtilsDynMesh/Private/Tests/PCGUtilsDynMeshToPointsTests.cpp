// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Conversion/PCGDynMeshToPoints.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshToPointsTests
{
	using namespace UE::Geometry;

	void SetCornerVectors(FDynamicMeshNormalOverlay* Overlay, int32 Triangle, const FVector3f& Vector)
	{
		FIndex3i Elements;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			Elements[Corner] = Overlay->AppendElement(Vector);
		}
		Overlay->SetTriangle(Triangle, Elements);
	}

	UPCGDynamicMeshData* Triangle(const FVector3f* Tangent)
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();
		Mesh.AppendVertex(FVector3d(2, 3, 4));
		Mesh.AppendVertex(FVector3d(12, 3, 4));
		Mesh.AppendVertex(FVector3d(2, 13, 4));
		const int32 TID = Mesh.AppendTriangle(0, 1, 2);
		SetCornerVectors(Mesh.Attributes()->PrimaryNormals(), TID, FVector3f::UnitZ());
		if (Tangent)
		{
			Mesh.Attributes()->EnableTangents();
			SetCornerVectors(Mesh.Attributes()->PrimaryTangents(), TID, *Tangent);
		}
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	TArray<FTransform> Run(UPCGDynamicMeshData* Input, bool bWorldSpace = false)
	{
		UPCGDynMeshToPointsSettings* Settings = NewObject<UPCGDynMeshToPointsSettings>();
		Settings->bOutputToWorldSpace = bWorldSpace;
		FPCGDataCollection Collection;
		FPCGTaggedData& Main = Collection.TaggedData.Emplace_GetRef();
		Main.Pin = PCGDynMeshToPointsConstants::InDynamicMeshLabel;
		Main.Data = Input;
		Collection.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Collection, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (!Element->Execute(Context.Get()))
			{
				continue;
			}
			TArray<FTransform> Result;
			for (const FPCGTaggedData& Output : Context->OutputData.TaggedData)
			{
				if (const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Output.Data))
				{
					const auto Transforms = Points->GetConstTransformValueRange();
					for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
					{
						Result.Add(Transforms[Index]);
					}
				}
			}
			return Result;
		}
		return {};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshToPointsFrameTest, "PCGUtils.DynMesh.ToPoints.TangentFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshToPointsFrameTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshToPointsTests;
	// Includes a normal component to check orthogonalization while preserving the normal as Z.
	const FVector3f Tangent(0, 3, 2);
	const TArray<FTransform> Points = Run(Triangle(&Tangent));
	TestEqual(TEXT("One point per vertex"), Points.Num(), 3);
	for (const FTransform& Point : Points)
	{
		TestTrue(TEXT("Point X follows the projected tangent"), Point.GetUnitAxis(EAxis::X).Equals(FVector::YAxisVector, 1e-6));
		TestTrue(TEXT("Point Z follows the normal"), Point.GetUnitAxis(EAxis::Z).Equals(FVector::ZAxisVector, 1e-6));
		TestTrue(TEXT("Rotation is normalized"), Point.GetRotation().IsNormalized());
	}
	if (!Points.IsEmpty())
	{
		TestTrue(TEXT("Local position is preserved"), Points[0].GetLocation().Equals(FVector(2, 3, 4)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshToPointsFallbackTest, "PCGUtils.DynMesh.ToPoints.TangentFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshToPointsFallbackTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshToPointsTests;
	const FVector3f Zero = FVector3f::ZeroVector;
	const FVector3f Parallel = FVector3f::UnitZ();
	UPCGDynamicMeshData* Missing = Triangle(nullptr);
	UPCGDynamicMeshData* Unset = Triangle(nullptr);
	Unset->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh) { Mesh.Attributes()->EnableTangents(); });
	UPCGDynamicMeshData* Seam = Triangle(nullptr);
	Seam->GetMutableDynamicMesh()->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.AppendVertex(FVector3d(12, 13, 4));
		const int32 Second = Mesh.AppendTriangle(1, 3, 2);
		SetCornerVectors(Mesh.Attributes()->PrimaryNormals(), Second, FVector3f::UnitY());
		Mesh.Attributes()->EnableTangents();
		SetCornerVectors(Mesh.Attributes()->PrimaryTangents(), Second, -FVector3f::UnitX());
		// Both triangles share vertices 1 and 2, but the first triangle has no tangent.
	});
	const FQuat Expected = FRotationMatrix::MakeFromZ(FVector::ZAxisVector).ToQuat();
	for (UPCGDynamicMeshData* Input : { Missing, Unset, Triangle(&Zero), Triangle(&Parallel), Seam })
	{
		const TArray<FTransform> Points = Run(Input);
		TestEqual(TEXT("Fallback retains all vertices"), Points.Num(), Input->GetDynamicMesh()->GetMeshRef().VertexCount());
		int32 SharedVerticesUsingFirstFace = 0;
		for (int32 Index = 0; Index < FMath::Min(3, Points.Num()); ++Index)
		{
			// Adjacency enumeration can choose either face at a seam. Verify the two directions stay paired.
			if (Input == Seam && Points[Index].GetUnitAxis(EAxis::Z).Equals(FVector::YAxisVector, 1e-6))
			{
				TestTrue(TEXT("Second-face normal retains its own tangent"), Points[Index].GetUnitAxis(EAxis::X).Equals(-FVector::XAxisVector, 1e-6));
			}
			else
			{
				TestTrue(TEXT("Unavailable tangents retain the normal-only rotation"), Points[Index].GetRotation().Equals(Expected, 1e-6));
				if (Input == Seam && Index > 0)
				{
					++SharedVerticesUsingFirstFace;
				}
			}
		}
		if (Input == Seam)
		{
			TestTrue(TEXT("Fixture exercises a shared vertex whose chosen normal has no matching tangent"), SharedVerticesUsingFirstFace > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshToPointsWorldTest, "PCGUtils.DynMesh.ToPoints.WorldFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshToPointsWorldTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshToPointsTests;
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Editor))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}
	ON_SCOPE_EXIT { WorldWrapper.DestroyTestWorld(true); };
	AActor* Actor = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Actor);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	const FTransform Placement(FRotator(23, 41, 17), FVector(100, -50, 200), FVector(2));
	Actor->SetActorTransform(Placement);
	const FVector3f Tangent = FVector3f::UnitY();
	UPCGDynamicMeshData* Input = Triangle(&Tangent);
	Input->TargetActor = Actor;
	const TArray<FTransform> Points = Run(Input, true);
	TestEqual(TEXT("World-space point count"), Points.Num(), 3);
	for (const FTransform& Point : Points)
	{
		TestTrue(TEXT("Actor rotation transforms the tangent"), Point.GetUnitAxis(EAxis::X).Equals(Placement.TransformVectorNoScale(FVector::YAxisVector), 1e-6));
		TestTrue(TEXT("Actor rotation transforms the normal"), Point.GetUnitAxis(EAxis::Z).Equals(Placement.TransformVectorNoScale(FVector::ZAxisVector), 1e-6));
	}
	if (!Points.IsEmpty())
	{
		TestTrue(TEXT("Actor transform places the vertex"), Points[0].GetLocation().Equals(Placement.TransformPosition(FVector(2, 3, 4)), 1e-6));
	}
	return true;
}

#endif
