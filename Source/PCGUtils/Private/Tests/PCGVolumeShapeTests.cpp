// Copyright Max Harris
//
// The shapes a PCG Volume Component encloses: point tests, meshes and the volume data made from them.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGVolumeShape.h"
#include "Data/PCGVolumeShapeData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "PCGPoint.h"

namespace PCGVolumeShapeTests
{
	FPCGVolumeShapeDesc MakeShape(EPCGVolumeShape Shape, const FTransform& Transform = FTransform::Identity)
	{
		FPCGVolumeShapeDesc Desc;
		Desc.Shape = Shape;
		Desc.BoxExtent = FVector(100, 50, 25);
		Desc.Radius = 50.0;
		Desc.HalfHeight = 100.0;
		Desc.Sides = 32;
		Desc.ShapeToWorld = Transform;
		return Desc;
	}

	double SignedVolume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Six = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Six += A.Dot(B.Cross(C));
		}
		return Six / 6.0;
	}

	int32 CountOpenEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.BoundaryEdgeIndicesItr())
		{
			(void)EdgeID;
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVolumeShapeContainsTest,
	"PCGUtils.VolumeShape.ContainsTheExactShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVolumeShapeContainsTest::RunTest(const FString&)
{
	using namespace PCGVolumeShapeTests;

	const FPCGVolumeShapeDesc Box = MakeShape(EPCGVolumeShape::Box);
	TestTrue(TEXT("Box holds a point inside its extent"), Box.ContainsLocal(FVector(99, 49, 24)));
	TestFalse(TEXT("Box does not hold a point past its Y extent"), Box.ContainsLocal(FVector(0, 51, 0)));

	const FPCGVolumeShapeDesc Sphere = MakeShape(EPCGVolumeShape::Sphere);
	TestTrue(TEXT("Sphere holds a point inside its radius"), Sphere.ContainsLocal(FVector(30, 30, 0)));
	TestFalse(TEXT("Sphere does not hold its bounding box corner"), Sphere.ContainsLocal(FVector(45, 45, 0)));

	const FPCGVolumeShapeDesc Cylinder = MakeShape(EPCGVolumeShape::Cylinder);
	TestTrue(TEXT("Cylinder holds a point near its top rim"), Cylinder.ContainsLocal(FVector(30, 30, 99)));
	TestFalse(TEXT("Cylinder does not hold a point above it"), Cylinder.ContainsLocal(FVector(0, 0, 101)));

	const FPCGVolumeShapeDesc Cone = MakeShape(EPCGVolumeShape::Cone);
	TestTrue(TEXT("Cone holds a point near its base rim"), Cone.ContainsLocal(FVector(45, 0, -99)));
	TestFalse(TEXT("Cone does not hold the same radius near its tip"), Cone.ContainsLocal(FVector(45, 0, 90)));

	const FPCGVolumeShapeDesc Capsule = MakeShape(EPCGVolumeShape::Capsule);
	TestTrue(TEXT("Capsule holds a point on its axis near the end"), Capsule.ContainsLocal(FVector(0, 0, 99)));
	TestFalse(TEXT("Capsule does not hold the corner its rounded end cuts off"), Capsule.ContainsLocal(FVector(45, 0, 95)));

	// Placement: rotated 90 degrees about Z, scaled 2 along its own X, moved 1000 along world X.
	const FTransform Placed(FRotator(0, 90, 0), FVector(1000, 0, 0), FVector(2, 1, 1));
	const FPCGVolumeShapeDesc PlacedBox = MakeShape(EPCGVolumeShape::Box, Placed);
	TestTrue(TEXT("A placed box's scaled X extent (200) now lies along world Y"), PlacedBox.Contains(FVector(1000, 190, 0)));
	TestFalse(TEXT("A placed box no longer reaches 100 along world X"), PlacedBox.Contains(FVector(1090, 0, 0)));
	const FBox WorldBounds = PlacedBox.GetWorldBounds();
	TestTrue(*FString::Printf(TEXT("Its world bounds are 50 by 200 by 25 around (1000, 0, 0) (%s)"), *WorldBounds.ToString()),
		WorldBounds.GetCenter().Equals(FVector(1000, 0, 0), 0.01) && WorldBounds.GetExtent().Equals(FVector(50, 200, 25), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVolumeShapeMeshTest,
	"PCGUtils.VolumeShape.MeshesAreClosedOutwardAndTheRightSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVolumeShapeMeshTest::RunTest(const FString&)
{
	using namespace PCGVolumeShapeTests;

	// Expected volumes of the exact shapes; 32 sides should land within a few percent.
	const double R = 50.0, H = 100.0;
	const TPair<EPCGVolumeShape, double> Expected[] = {
		{ EPCGVolumeShape::Box, 200.0 * 100.0 * 50.0 },
		{ EPCGVolumeShape::Sphere, 4.0 / 3.0 * UE_DOUBLE_PI * R * R * R },
		{ EPCGVolumeShape::Cylinder, UE_DOUBLE_PI * R * R * 2.0 * H },
		{ EPCGVolumeShape::Cone, UE_DOUBLE_PI * R * R * 2.0 * H / 3.0 },
		{ EPCGVolumeShape::Capsule, UE_DOUBLE_PI * R * R * 2.0 * (H - R) + 4.0 / 3.0 * UE_DOUBLE_PI * R * R * R },
	};

	// The engine's own box decides which way outward is.
	UE::Geometry::FMinimalBoxMeshGenerator Generator;
	Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(1.0));
	Generator.Generate();
	const double OutwardSign = FMath::Sign(SignedVolume(UE::Geometry::FDynamicMesh3(&Generator)));

	for (const TPair<EPCGVolumeShape, double>& Case : Expected)
	{
		const FString Name = StaticEnum<EPCGVolumeShape>()->GetNameStringByValue(static_cast<int64>(Case.Key));
		UE::Geometry::FDynamicMesh3 Mesh;
		MakeShape(Case.Key).BuildMesh(Mesh, /*bWorldSpace=*/false);
		UTEST_TRUE(*FString::Printf(TEXT("%s gives a mesh"), *Name), Mesh.TriangleCount() > 0);
		TestEqual(*FString::Printf(TEXT("%s is closed"), *Name), CountOpenEdges(Mesh), 0);
		const double Volume = SignedVolume(Mesh);
		TestTrue(*FString::Printf(TEXT("%s faces outward like the engine's box"), *Name), Volume * OutwardSign > 0.0);
		TestTrue(*FString::Printf(TEXT("%s encloses about %.0f (%.0f)"), *Name, Case.Value, FMath::Abs(Volume)),
			FMath::IsNearlyEqual(FMath::Abs(Volume), Case.Value, Case.Value * 0.05));
	}

	UE::Geometry::FDynamicMesh3 Owner;
	MakeShape(EPCGVolumeShape::OwnerVolume).BuildMesh(Owner, false);
	TestEqual(TEXT("Owner Volume has no mesh of its own"), Owner.TriangleCount(), 0);

	// World space follows the placement, including a mirror.
	UE::Geometry::FDynamicMesh3 Placed;
	MakeShape(EPCGVolumeShape::Cone, FTransform(FRotator::ZeroRotator, FVector(0, 0, 500), FVector(1, 1, -1))).BuildMesh(Placed, true);
	TestTrue(*FString::Printf(TEXT("A mirrored cone sits around Z 500 (%s)"), *FBox(Placed.GetBounds()).ToString()),
		FBox(Placed.GetBounds()).GetCenter().Equals(FVector(0, 0, 500), 0.5));
	double PlacedVolume = 0.0;
	for (const int32 TriangleID : Placed.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Placed.GetTriVertices(TriangleID, A, B, C);
		const FVector3d Centre(0, 0, 500);
		PlacedVolume += (A - Centre).Dot((B - Centre).Cross(C - Centre));
	}
	TestTrue(TEXT("A mirrored cone still faces outward"), PlacedVolume * OutwardSign > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVolumeShapeDataTest,
	"PCGUtils.VolumeShape.VolumeDataSamplesTheShapeNotItsBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVolumeShapeDataTest::RunTest(const FString&)
{
	using namespace PCGVolumeShapeTests;

	// A sphere moved to (0, 0, 300).
	UPCGVolumeShapeData* Data = NewObject<UPCGVolumeShapeData>();
	Data->Initialize(MakeShape(EPCGVolumeShape::Sphere, FTransform(FVector(0, 0, 300))));
	TestTrue(*FString::Printf(TEXT("Its bounds hold the sphere (%s)"), *Data->GetBounds().ToString()),
		Data->GetBounds().GetCenter().Equals(FVector(0, 0, 300), 0.01) && Data->GetBounds().GetExtent().Equals(FVector(50), 0.01));

	FPCGPoint Point;
	TestTrue(TEXT("A point inside the sphere samples with density 1"),
		Data->SamplePoint(FTransform(FVector(10, 10, 310)), FBox(FVector(-1), FVector(1)), Point, nullptr) && Point.Density == 1.0f);
	TestFalse(TEXT("A point in the bounds corner but outside the sphere does not sample"),
		Data->SamplePoint(FTransform(FVector(45, 45, 345)), FBox(FVector(-1), FVector(1)), Point, nullptr));
	TestFalse(TEXT("A point outside the bounds does not sample"),
		Data->SamplePoint(FTransform(FVector(0, 0, 0)), FBox(FVector(-1), FVector(1)), Point, nullptr));

	const UPCGVolumeShapeData* Copy = Cast<UPCGVolumeShapeData>(Data->DuplicateData(nullptr));
	UTEST_NOT_NULL(TEXT("A copy is still shape volume data"), Copy);
	TestEqual(TEXT("The copy keeps the shape"), Copy->GetShape().Shape, EPCGVolumeShape::Sphere);
	TestFalse(TEXT("The copy still tests the shape, not the box"),
		Copy->SamplePoint(FTransform(FVector(45, 45, 345)), FBox(FVector(-1), FVector(1)), Point, nullptr));
	return true;
}

#endif
