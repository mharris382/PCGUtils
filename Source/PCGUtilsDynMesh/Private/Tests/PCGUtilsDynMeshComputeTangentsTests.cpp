// Copyright Max Harris
//
// DynMesh | Compute Tangents: a mesh assembled from pieces with and without tangents (the CourseGen green after an
// Embedded Shape boolean: the green has tangents, the cut-in faces arrive without them and flipped) comes out with
// one consistent tangent frame on every triangle.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTangents.h"
#include "DynamicMeshEditor.h"
#include "Elements/Attributes/PCGDynMeshComputeTangents.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshComputeTangentsTests
{
	using namespace UE::Geometry;

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshComputeTangentsTest, "PCGUtils.DynMesh.ComputeTangents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsDynMeshComputeTangentsTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsDynMeshComputeTangentsTests;

	UPCGDynamicMeshData* Input = NewObject<UPCGDynamicMeshData>();
	UDynamicMesh* Mesh = Input->GetMutableDynamicMesh();

	// The "green": a flat grid facing up, given MikkT tangents.
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(Mesh, FGeometryScriptPrimitiveOptions(),
		FTransform(FVector(0.0, 0.0, 0.0)), 200.0, 200.0, 3, 3);
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(Mesh, FGeometryScriptTangentsOptions());
	const int32 GreenTriangles = Mesh->GetTriangleCount();

	// The "cut-in faces": built facing down (as a Difference shape is), appended without tangents, then reversed
	// with their normals the way the boolean reverses them, so they face up beside the green.
	{
		FDynamicMesh3 Cut;
		Cut.EnableAttributes();
		const int32 V0 = Cut.AppendVertex(FVector3d(300, -100, 0));
		const int32 V1 = Cut.AppendVertex(FVector3d(500, -100, 0));
		const int32 V2 = Cut.AppendVertex(FVector3d(500, 100, 0));
		const int32 V3 = Cut.AppendVertex(FVector3d(300, 100, 0));
		// Facing down: (C - A) x (B - A) points -Z for these windings.
		Cut.AppendTriangle(V0, V1, V2);
		Cut.AppendTriangle(V0, V2, V3);
		FDynamicMeshUVOverlay* UVs = Cut.Attributes()->PrimaryUV();
		FDynamicMeshNormalOverlay* Normals = Cut.Attributes()->PrimaryNormals();
		for (const int32 Tri : Cut.TriangleIndicesItr())
		{
			const FIndex3i T = Cut.GetTriangle(Tri);
			FIndex3i UVTri, NTri;
			for (int32 J = 0; J < 3; ++J)
			{
				const FVector3d P = Cut.GetVertex(T[J]);
				UVTri[J] = UVs->AppendElement(FVector2f(static_cast<float>(P.X * 0.01), static_cast<float>(P.Y * 0.01)));
				NTri[J] = Normals->AppendElement(FVector3f(0, 0, -1));
			}
			UVs->SetTriangle(Tri, UVTri);
			Normals->SetTriangle(Tri, NTri);
		}
		TArray<int32> All;
		for (const int32 Tri : Cut.TriangleIndicesItr()) { All.Add(Tri); }
		FDynamicMeshEditor(&Cut).ReverseTriangleOrientations(All, true);

		Mesh->EditMesh([&Cut](FDynamicMesh3& Target)
		{
			FMeshIndexMappings Maps;
			FDynamicMeshEditor(&Target).AppendMesh(&Cut, Maps);
		});
	}
	TestTrue(TEXT("Cut faces were appended"), Mesh->GetTriangleCount() > GreenTriangles);

	UPCGDynMeshComputeTangentsSettings* Settings = NewObject<UPCGDynMeshComputeTangentsSettings>();
	const TArray<FPCGTaggedData> Output = Run(Settings, Input);
	TestEqual(TEXT("One output"), Output.Num(), 1);
	const UPCGDynamicMeshData* OutData = Output.Num() == 1 ? Cast<UPCGDynamicMeshData>(Output[0].Data) : nullptr;
	if (!TestNotNull(TEXT("Output is DynMesh data"), OutData))
	{
		return false;
	}

	const FDynamicMesh3& Result = OutData->GetDynamicMesh()->GetMeshRef();
	if (!TestTrue(TEXT("Output has tangents"), Result.HasAttributes() && Result.Attributes()->HasTangentSpace()))
	{
		return false;
	}

	// Every triangle, green or cut, has the same frame: normal up, tangent along +U (world X), bitangent along +V.
	const FDynamicMeshTangents Tangents(&Result);
	const FDynamicMeshNormalOverlay* Normals = Result.Attributes()->PrimaryNormals();
	FVector3f FirstX = FVector3f::ZeroVector;
	FVector3f FirstY = FVector3f::ZeroVector;
	bool bHaveFirst = false;
	int32 Mismatches = 0;
	for (const int32 Tri : Result.TriangleIndicesItr())
	{
		for (int32 J = 0; J < 3; ++J)
		{
			const FVector3f N = Normals->GetElement(Normals->GetTriangle(Tri)[J]);
			FVector3f TX, TY;
			Tangents.GetTangentVectors(Tri, J, N, TX, TY);
			if (!bHaveFirst)
			{
				FirstX = TX;
				FirstY = TY;
				bHaveFirst = true;
			}
			const bool bOk = TX.Dot(FirstX) > 0.99f && TY.Dot(FirstY) > 0.99f && FMath::Abs(TX.Dot(N)) < 0.01f;
			if (!bOk)
			{
				++Mismatches;
				if (Mismatches <= 3)
				{
					AddError(FString::Printf(TEXT("Triangle %d corner %d: tangent %s bitangent %s, expected %s / %s"),
						Tri, J, *TX.ToString(), *TY.ToString(), *FirstX.ToString(), *FirstY.ToString()));
				}
			}
		}
	}
	TestEqual(TEXT("Corners with a different tangent frame"), Mismatches, 0);
	TestTrue(TEXT("The tangent follows world X"), FMath::Abs(FirstX.X) > 0.99f);
	return true;
}

#endif
