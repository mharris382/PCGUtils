// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPropertiesTests
{
	template <typename T>
	bool ReadData(const UPCGDynamicMeshData& Data, FName Name, T& OutValue)
	{
		const FPCGMetadataDomain* Domain = Data.ConstMetadata()
			? Data.ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttribute<T>* Attribute = Domain ? Domain->GetConstTypedAttribute<T>(Name) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(Domain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey);
		return true;
	}

	UPCGDynamicMeshData* MakeBox()
	{
		UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			Mesh, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 100.0f, 100.0f, 100.0f);
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(Mesh, /*bCanTakeOwnership=*/true);
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshPropertiesTest,
	"PCGUtils.DynMesh.Properties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPropertiesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPropertiesTests;

	UPCGDynamicMeshData* Data = MakeBox();
	const UE::Geometry::FDynamicMesh3& Mesh = *Data->GetDynamicMesh()->GetMeshPtr();

	FPCGUtilsDynMeshFreeProperties Free;
	Free.bOutputTriangleCount = true;
	Free.bOutputVertexCount = true;
	Free.bOutputEulerCharacteristic = true;
	Free.bOutputHasNormals = true;
	Free.bOutputVertexCount = true;
	Free.VertexCountAttributeName = TEXT("PrevVertCount");
	TestTrue(TEXT("Free properties validate"), PCGUtilsDynMeshProperties::Validate(Free, nullptr));
	PCGUtilsDynMeshProperties::Write(*Data, Free);

	int32 Triangles = 0, Vertices = 0, Euler = 0;
	bool bHasNormals = false;
	TestTrue(TEXT("TriangleCount written"), ReadData(*Data, TEXT("TriangleCount"), Triangles));
	TestEqual(TEXT("TriangleCount matches the mesh"), Triangles, Mesh.TriangleCount());
	TestTrue(TEXT("Renamed vertex count written under its new name"), ReadData(*Data, TEXT("PrevVertCount"), Vertices));
	TestEqual(TEXT("Vertex count matches the mesh"), Vertices, Mesh.VertexCount());
	int32 Unused = 0;
	TestFalse(TEXT("Default vertex count name not written when renamed"), ReadData(*Data, TEXT("VertexCount"), Unused));
	TestTrue(TEXT("Euler characteristic written"), ReadData(*Data, TEXT("EulerCharacteristic"), Euler));
	TestEqual(TEXT("A closed box has Euler characteristic 2"), Euler, 2);
	TestTrue(TEXT("HasNormals written"), ReadData(*Data, TEXT("HasNormals"), bHasNormals) && bHasNormals);
	TestFalse(TEXT("Disabled properties are not written"), ReadData(*Data, TEXT("EdgeCount"), Unused));

	FPCGUtilsDynMeshComputedProperties Computed;
	Computed.bOutputSurfaceArea = true;
	Computed.bOutputVolume = true;
	Computed.bOutputIsClosed = true;
	Computed.bOutputConnectedComponentCount = true;
	Computed.bOutputInvertedNormalTriangleCount = true;
	Computed.bOutputMinTriangleAngle = true;
	Computed.bOutputBoundsMax = true;
	PCGUtilsDynMeshProperties::Write(*Data, Computed);

	double Area = 0.0, Volume = 0.0, MinAngle = 0.0;
	bool bClosed = false;
	int32 Components = 0, Inverted = -1;
	FVector BoundsMax = FVector::ZeroVector;
	ReadData(*Data, TEXT("SurfaceArea"), Area);
	ReadData(*Data, TEXT("Volume"), Volume);
	ReadData(*Data, TEXT("IsClosed"), bClosed);
	ReadData(*Data, TEXT("ConnectedComponentCount"), Components);
	ReadData(*Data, TEXT("InvertedNormalTriangleCount"), Inverted);
	ReadData(*Data, TEXT("MinTriangleAngle"), MinAngle);
	ReadData(*Data, TEXT("BoundsMax"), BoundsMax);
	TestEqual(TEXT("Box surface area"), Area, 60000.0, 0.01);
	TestEqual(TEXT("Box volume"), Volume, 1000000.0, 0.1);
	TestTrue(TEXT("Box is closed"), bClosed);
	TestEqual(TEXT("Box is one piece"), Components, 1);
	TestEqual(TEXT("Box has no inverted-normal triangles"), Inverted, 0);
	TestEqual(TEXT("Box triangles have 45 degree corners"), MinAngle, 45.0, 0.001);
	TestEqual(TEXT("Box bounds max Z"), BoundsMax.Z, 100.0, 0.001);

	// Flip one triangle's winding without touching its stored normals: exactly the fold-over the count is for.
	Data->GetMutableDynamicMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& EditMesh)
	{
		const int32 TriangleID = *EditMesh.TriangleIndicesItr().begin();
		const UE::Geometry::FIndex3i Tri = EditMesh.GetTriangle(TriangleID);
		EditMesh.SetTriangle(TriangleID, UE::Geometry::FIndex3i(Tri.A, Tri.C, Tri.B), /*bRemoveIsolatedVertices=*/false);
	});
	PCGUtilsDynMeshProperties::Write(*Data, Computed);
	ReadData(*Data, TEXT("InvertedNormalTriangleCount"), Inverted);
	TestEqual(TEXT("A flipped triangle is counted as inverted"), Inverted, 1);

	// Rewriting a name replaces the earlier attribute rather than failing on a duplicate.
	Free.TriangleCountAttributeName = TEXT("SurfaceArea");
	PCGUtilsDynMeshProperties::Write(*Data, Free);
	TestTrue(TEXT("An attribute of another type is replaced"), ReadData(*Data, TEXT("SurfaceArea"), Triangles));
	return true;
}

#endif
