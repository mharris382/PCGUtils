// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Topology/PCGRemesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

/**
 * Selection-region processes (Remesh, Simplify, Decimate, Mirror, Array, Planar Cut, Clear Polygroups) extract the
 * selected triangles, operate on them, and weld them back with FMeshRegionOperator::BackPropropagate, which copies
 * no attributes. Regression: a Remesh with a selection left the reinserted triangles with unset normals (faceted
 * shading), lost UVs/colors, reset material IDs to 0 and zeroed weight maps on new vertices.
 */
namespace PCGUtilsDynMeshRegionRestoreTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr int32 MaterialID = 3;
	constexpr float Weight = 0.5f;
	const FName WeightLayerName = TEXT("TestWeight");

	FDynamicMesh3 MakeSphere()
	{
		UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(Mesh, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 20, 32);
		FDynamicMesh3 Result = Mesh->GetMeshRef();
		Result.EnableAttributes();
		Result.Attributes()->EnableMaterialID();
		for (const int32 Tid : Result.TriangleIndicesItr())
		{
			Result.Attributes()->GetMaterialID()->SetValue(Tid, MaterialID);
		}
		Result.Attributes()->SetNumWeightLayers(1);
		FDynamicMeshWeightAttribute* Layer = Result.Attributes()->GetWeightLayer(0);
		Layer->SetName(WeightLayerName);
		for (const int32 Vid : Result.VertexIndicesItr())
		{
			Layer->SetValue(Vid, &Weight);
		}
		return Result;
	}

	int32 CountNormalSeamEdges(const FDynamicMesh3& Mesh)
	{
		const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		int32 Seams = 0;
		for (const int32 Eid : Mesh.EdgeIndicesItr())
		{
			Seams += Normals->IsSeamEdge(Eid) ? 1 : 0;
		}
		return Seams;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshRegionRestoreRemeshTest, "PCGUtils.DynMesh.MeshTarget.RegionRestoresAttributes",
	PCGUtilsDynMeshRegionRestoreTests::Flags)
bool FPCGUtilsDynMeshRegionRestoreRemeshTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshRegionRestoreTests;
	const FDynamicMesh3 Source = MakeSphere();
	TestEqual(TEXT("Precondition: the source sphere has smooth normals (no normal seams)"), CountNormalSeamEdges(Source), 0);

	UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
	Data->Initialize(FDynamicMesh3(Source));

	// The upper hemisphere, so the selection border runs around the equator through smooth surface.
	FGeometrySelection Selection;
	Selection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
	for (const int32 Tid : Source.TriangleIndicesItr())
	{
		if (Source.GetTriCentroid(Tid).Z > 0.0)
		{
			Selection.Selection.Add(FGeoSelectionID::MeshTriangle(Tid).Encoded());
		}
	}
	UPCGDynamicMeshSelectionData* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
	SelectionData->Initialize(Data, MoveTemp(Selection));

	UPCGRemeshSettings* Settings = NewObject<UPCGRemeshSettings>();
	Settings->Mode = EPCGRemeshMode::Uniform;
	Settings->UniformOptions.TargetType = EGeometryScriptUniformRemeshTargetType::TargetEdgeLength;
	Settings->UniformOptions.TargetEdgeLength = 5.0f;

	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.MeshData = Data;
	Invocation.SelectionData = SelectionData;
	FPCGUtilsDynMeshProcessOutcome Outcome;
	TestTrue(TEXT("Remesh with a selection executes"), Settings->CreateProcessOperation(nullptr)->Execute(Invocation, Outcome));

	const FDynamicMesh3& Result = Data->GetDynamicMesh()->GetMeshRef();
	TestTrue(TEXT("The selected region was remeshed (triangle count changed)"), Result.TriangleCount() != Source.TriangleCount());
	TestTrue(TEXT("Result mesh is valid"), Result.CheckValidity(FDynamicMesh3::FValidityOptions(), EValidityCheckFailMode::ReturnOnly));

	const FDynamicMeshAttributeSet* Attributes = Result.Attributes();
	int32 UnsetNormals = 0, UnsetUVs = 0, WrongMaterial = 0, BadNormal = 0;
	for (const int32 Tid : Result.TriangleIndicesItr())
	{
		UnsetNormals += Attributes->PrimaryNormals()->IsSetTriangle(Tid) ? 0 : 1;
		UnsetUVs += Attributes->PrimaryUV()->IsSetTriangle(Tid) ? 0 : 1;
		WrongMaterial += Attributes->GetMaterialID()->GetValue(Tid) == MaterialID ? 0 : 1;
		if (Attributes->PrimaryNormals()->IsSetTriangle(Tid))
		{
			const FIndex3i Verts = Result.GetTriangle(Tid);
			for (int32 j = 0; j < 3; ++j)
			{
				FVector3f Normal;
				Attributes->PrimaryNormals()->GetElementAtVertex(Tid, Verts[j], Normal);
				// A sphere centred on the origin: a correct smooth normal points along the vertex position.
				const FVector3d Expected = Normalized(Result.GetVertex(Verts[j]));
				BadNormal += FVector3d(Normal).Dot(Expected) > 0.98 ? 0 : 1;
			}
		}
	}
	TestEqual(TEXT("Every triangle has a normal overlay triangle"), UnsetNormals, 0);
	TestEqual(TEXT("Every triangle has a UV overlay triangle"), UnsetUVs, 0);
	TestEqual(TEXT("Every triangle keeps its material ID"), WrongMaterial, 0);
	TestEqual(TEXT("Every corner normal is smooth (within ~11 degrees of the sphere normal)"), BadNormal, 0);
	TestEqual(TEXT("The weld adds no normal seams"), CountNormalSeamEdges(Result), 0);

	int32 WrongWeights = 0;
	const FDynamicMeshWeightAttribute* Layer = Attributes->NumWeightLayers() > 0 ? Attributes->GetWeightLayer(0) : nullptr;
	TestNotNull(TEXT("Weight layer survives"), Layer);
	if (Layer)
	{
		for (const int32 Vid : Result.VertexIndicesItr())
		{
			float Value = 0.0f;
			Layer->GetValue(Vid, &Value);
			WrongWeights += FMath::IsNearlyEqual(Value, Weight, 1e-3f) ? 0 : 1;
		}
	}
	TestEqual(TEXT("Every vertex keeps its weight (new region vertices included)"), WrongWeights, 0);
	return true;
}

#endif
