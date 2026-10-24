// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Topology/PCGDynMeshDecimate.h"
#include "Geometry/PCGUtilsDynMeshDecimate.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "MeshQueries.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshDecimateTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// Geometry Script's Steps arguments count vertices along a side, so N segments need N + 1.
	FDynamicMesh3 MakeBox(int32 Segments, double Size = 100.0)
	{
		const int32 Steps = Segments + 1;
		UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, FGeometryScriptPrimitiveOptions(), FTransform::Identity,
			Size, Size, Size, Steps, Steps, Steps, EGeometryScriptPrimitiveOriginMode::Center);
		return Mesh->GetMeshRef();
	}

	FDynamicMesh3 MakeGrid(int32 Segments, double Size = 160.0)
	{
		const int32 Steps = Segments + 1;
		UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(Mesh, FGeometryScriptPrimitiveOptions(), FTransform::Identity,
			Size, Size, Steps, Steps);
		return Mesh->GetMeshRef();
	}

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	bool IsValidMesh(const FDynamicMesh3& Mesh)
	{
		return Mesh.CheckValidity(FDynamicMesh3::FValidityOptions(), EValidityCheckFailMode::ReturnOnly);
	}

	/** Smallest interior angle of any triangle, in degrees. */
	double MinAngle(const FDynamicMesh3& Mesh)
	{
		double Min = 180.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d P[3];
			Mesh.GetTriVertices(Tid, P[0], P[1], P[2]);
			for (int32 C = 0; C < 3; ++C)
			{
				const FVector3d A = Normalized(P[(C + 1) % 3] - P[C]);
				const FVector3d B = Normalized(P[(C + 2) % 3] - P[C]);
				Min = FMath::Min(Min, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.Dot(B), -1.0, 1.0))));
			}
		}
		return Min;
	}

	/** Every triangle normal agrees with the closest face of an axis-aligned box centered on the origin. */
	int32 CountFlippedOnBox(const FDynamicMesh3& Mesh)
	{
		int32 Flipped = 0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			const FVector3d Centroid = Mesh.GetTriCentroid(Tid);
			const FVector3d Normal = Mesh.GetTriNormal(Tid);
			const int32 Axis = FMath::Abs(Centroid.X) >= FMath::Abs(Centroid.Y) && FMath::Abs(Centroid.X) >= FMath::Abs(Centroid.Z) ? 0
				: (FMath::Abs(Centroid.Y) >= FMath::Abs(Centroid.Z) ? 1 : 2);
			Flipped += Normal[Axis] * Centroid[Axis] <= 0.0;
		}
		return Flipped;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimatePlanarBoxTest, "PCGUtils.DynMesh.Decimate.Planar.Box", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimatePlanarBoxTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	FDynamicMesh3 Mesh = MakeBox(4);
	const double SourceVolume = Volume(Mesh);
	PCGUtilsDynMeshDecimate::FPlanarOptions Options;
	Options.bDelimitUVSeams = false;
	const PCGUtilsDynMeshDecimate::FResult Result = PCGUtilsDynMeshDecimate::DissolvePlanar(Mesh, Options);

	TestEqual(TEXT("A subdivided box becomes 6 faces"), Result.Regions, 6);
	TestEqual(TEXT("Only the 8 corners remain"), Mesh.VertexCount(), 8);
	TestEqual(TEXT("Two triangles per face"), Mesh.TriangleCount(), 12);
	TestEqual(TEXT("Nothing blocked"), Result.BlockedVertices, 0);
	TestEqual(TEXT("Volume unchanged"), Volume(Mesh), SourceVolume, 1e-3);
	TestEqual(TEXT("No triangle faces inward"), CountFlippedOnBox(Mesh), 0);
	TestTrue(TEXT("Mesh is valid"), IsValidMesh(Mesh));
	TestTrue(TEXT("Attributes kept"), Mesh.HasAttributes() && Mesh.Attributes()->NumUVLayers() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimatePlanarDelimitTest, "PCGUtils.DynMesh.Decimate.Planar.DelimitAndSelection", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimatePlanarDelimitTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	// A flat 8x8 grid; the left half is material 1.
	FDynamicMesh3 Mesh = MakeGrid(8);
	Mesh.Attributes()->EnableMaterialID();
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		Mesh.Attributes()->GetMaterialID()->SetValue(Tid, Mesh.GetTriCentroid(Tid).X < 0.0 ? 1 : 0);
	}
	FDynamicMesh3 Delimited = Mesh;
	PCGUtilsDynMeshDecimate::FPlanarOptions Options;
	PCGUtilsDynMeshDecimate::FResult Result = PCGUtilsDynMeshDecimate::DissolvePlanar(Delimited, Options);
	TestEqual(TEXT("Material delimit keeps two faces"), Result.Regions, 2);
	TestEqual(TEXT("Two rectangles share their seam: 6 corners"), Delimited.VertexCount(), 6);
	TestTrue(TEXT("Delimited mesh is valid"), IsValidMesh(Delimited));

	FDynamicMesh3 Merged = Mesh;
	Options.bDelimitMaterial = false;
	Result = PCGUtilsDynMeshDecimate::DissolvePlanar(Merged, Options);
	TestEqual(TEXT("Without the delimit it is one face"), Result.Regions, 1);
	TestEqual(TEXT("One rectangle: 4 corners"), Merged.VertexCount(), 4);
	TestEqual(TEXT("One rectangle: 2 triangles"), Merged.TriangleCount(), 2);

	// Selection: only triangles with y > 0 may change.
	FDynamicMesh3 Partial = Mesh;
	TSet<int32> Selected;
	int32 UnselectedBefore = 0;
	for (const int32 Tid : Partial.TriangleIndicesItr())
	{
		if (Partial.GetTriCentroid(Tid).Y > 0.0) { Selected.Add(Tid); }
		else { ++UnselectedBefore; }
	}
	Options.bDelimitMaterial = false;
	PCGUtilsDynMeshDecimate::DissolvePlanar(Partial, Options, &Selected);
	int32 UnselectedAfter = 0;
	for (const int32 Tid : Partial.TriangleIndicesItr())
	{
		UnselectedAfter += Partial.GetTriCentroid(Tid).Y < 0.0;
	}
	TestEqual(TEXT("Unselected triangles are untouched"), UnselectedAfter, UnselectedBefore);
	TestTrue(TEXT("Selected half was reduced"), Partial.TriangleCount() < Mesh.TriangleCount());
	TestTrue(TEXT("Partial mesh is valid"), IsValidMesh(Partial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimatePlanarCurvedTest, "PCGUtils.DynMesh.Decimate.Planar.AngleLimit", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimatePlanarCurvedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	// 16 sides, 22.5 degrees apart, each subdivided along its height.
	UDynamicMesh* Cylinder = NewObject<UDynamicMesh>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Cylinder, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 100, 16, 4, true);
	const FDynamicMesh3 Source = Cylinder->GetMeshRef();

	FDynamicMesh3 Small = Source;
	PCGUtilsDynMeshDecimate::FPlanarOptions Options;
	Options.bDelimitUVSeams = false;
	Options.bDelimitNormalSeams = false;
	PCGUtilsDynMeshDecimate::DissolvePlanar(Small, Options);
	TestEqual(TEXT("5 degrees keeps all 16 sides and both caps: 32 rim vertices"), Small.VertexCount(), 32);
	TestTrue(TEXT("Height subdivisions are gone"), Small.VertexCount() < Source.VertexCount());
	TestEqual(TEXT("Volume unchanged"), Volume(Small), Volume(Source), 1e-2);
	TestTrue(TEXT("Mesh is valid"), IsValidMesh(Small));

	FDynamicMesh3 Large = Source;
	Options.AngleLimitDegrees = 50.0;
	PCGUtilsDynMeshDecimate::DissolvePlanar(Large, Options);
	TestTrue(TEXT("50 degrees merges sides further"), Large.TriangleCount() < Small.TriangleCount());
	TestTrue(TEXT("Large-limit mesh is valid"), IsValidMesh(Large));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimateUnsubdivideGridTest, "PCGUtils.DynMesh.Decimate.UnSubdivide.Grid", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimateUnsubdivideGridTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	FDynamicMesh3 Mesh = MakeGrid(8);
	const int32 Segments = FMath::RoundToInt32(FMath::Sqrt(static_cast<double>(Mesh.VertexCount()))) - 1;
	TestEqual(TEXT("Grid has 8 segments a side"), Segments, 8);

	PCGUtilsDynMeshDecimate::FResult Result = PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, 1);
	TestEqual(TEXT("One pass: 16 face centers"), Result.GridCenters, 16);
	TestEqual(TEXT("One pass: 5x5 vertices"), Mesh.VertexCount(), 25);
	TestEqual(TEXT("One pass: 4x4 quads"), Mesh.TriangleCount(), 32);
	bool bOnCoarseGrid = true;
	for (const int32 Vid : Mesh.VertexIndicesItr())
	{
		const FVector3d P = Mesh.GetVertex(Vid);
		bOnCoarseGrid &= FMath::IsNearlyZero(FMath::Fmod(P.X + 80.0, 40.0), 1e-3) && FMath::IsNearlyZero(FMath::Fmod(P.Y + 80.0, 40.0), 1e-3);
	}
	TestTrue(TEXT("Kept vertices are every other original vertex"), bOnCoarseGrid);
	TestTrue(TEXT("Mesh is valid"), IsValidMesh(Mesh));

	Result = PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, 5);
	TestEqual(TEXT("Further passes stop at one quad"), Mesh.TriangleCount(), 2);
	TestEqual(TEXT("Further passes: 4 corners"), Mesh.VertexCount(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimateUnsubdivideBoxTest, "PCGUtils.DynMesh.Decimate.UnSubdivide.Box", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimateUnsubdivideBoxTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	FDynamicMesh3 Mesh = MakeBox(4);
	const int32 SourceTris = Mesh.TriangleCount();
	const double SourceVolume = Volume(Mesh);
	PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, 1);
	TestEqual(TEXT("One pass quarters the quads"), Mesh.TriangleCount(), SourceTris / 4);
	PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, 4);
	TestEqual(TEXT("Repeated passes reach the plain box"), Mesh.TriangleCount(), 12);
	TestEqual(TEXT("Plain box corners"), Mesh.VertexCount(), 8);
	TestEqual(TEXT("Volume unchanged"), Volume(Mesh), SourceVolume, 1e-3);
	TestEqual(TEXT("No triangle faces inward"), CountFlippedOnBox(Mesh), 0);
	TestTrue(TEXT("Mesh is valid"), IsValidMesh(Mesh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimateNodeTest, "PCGUtils.DynMesh.Decimate.Node", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimateNodeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	UPCGDynMeshDecimateSettings* Settings = NewObject<UPCGDynMeshDecimateSettings>();
	TestEqual(TEXT("Palette title"), Settings->GetDefaultNodeTitle().ToString(), FString(TEXT("DynMesh | Decimate")));

	auto Run = [&](const FDynamicMesh3& Source, const UPCGDynamicMeshSelectionData* Selection = nullptr)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(FDynamicMesh3(Source));
		FPCGUtilsDynMeshProcessInvocation Invocation;
		Invocation.MeshData = Data;
		Invocation.SelectionData = Selection;
		FPCGUtilsDynMeshProcessOutcome Outcome;
		TestTrue(TEXT("Decimate executes"), Settings->CreateProcessOperation(nullptr)->Execute(Invocation, Outcome));
		return FDynamicMesh3(Data->GetDynamicMesh()->GetMeshRef());
	};

	UDynamicMesh* Sphere = NewObject<UDynamicMesh>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(Sphere, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 20, 32);
	const int32 SphereTris = Sphere->GetTriangleCount();
	Settings->Mode = EPCGUtilsDynMeshDecimateMode::Collapse;
	Settings->Ratio = 0.25f;
	const FDynamicMesh3 Collapsed = Run(Sphere->GetMeshRef());
	TestTrue(TEXT("Collapse 0.25 keeps about a quarter of the triangles"),
		FMath::Abs(Collapsed.TriangleCount() - SphereTris / 4) <= SphereTris / 50 + 2);

	Settings->bUseWeightMap = true;
	Settings->WeightMapName = TEXT("NoSuchMap");
	{
		AddExpectedError(TEXT("no vertex weight map"), EAutomationExpectedErrorFlags::Contains, 0);
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(FDynamicMesh3(Sphere->GetMeshRef()));
		FPCGUtilsDynMeshProcessInvocation Invocation;
		Invocation.MeshData = Data;
		FPCGUtilsDynMeshProcessOutcome Outcome;
		TestFalse(TEXT("A missing weight map fails"), Settings->CreateProcessOperation(nullptr)->Execute(Invocation, Outcome));
	}
	Settings->bUseWeightMap = false;

	Settings->Mode = EPCGUtilsDynMeshDecimateMode::Planar;
	Settings->bDelimitUVSeams = false;
	TestEqual(TEXT("Node Planar reduces a subdivided box to 12 triangles"), Run(MakeBox(3)).TriangleCount(), 12);

	Settings->Mode = EPCGUtilsDynMeshDecimateMode::UnSubdivide;
	Settings->Iterations = 2;
	TestEqual(TEXT("Node Un-Subdivide x2 reduces an 8x8 grid to 2x2"), Run(MakeGrid(8)).TriangleCount(), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimateCompareTest, "PCGUtils.DynMesh.Decimate.Planar.CompareWithSimplifyPlanar", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimateCompareTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	struct FCase { const TCHAR* Name; FDynamicMesh3 Mesh; };
	TArray<FCase> Cases;
	Cases.Add({TEXT("Box 6 steps"), MakeBox(6)});
	Cases.Add({TEXT("Grid 12 steps"), MakeGrid(12)});
	{
		UDynamicMesh* Cylinder = NewObject<UDynamicMesh>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Cylinder, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 100, 24, 6, true);
		Cases.Add({TEXT("Cylinder 24x6"), Cylinder->GetMeshRef()});
	}
	for (FCase& Case : Cases)
	{
		UDynamicMesh* Engine = NewObject<UDynamicMesh>();
		Engine->SetMesh(Case.Mesh);
		FGeometryScriptPlanarSimplifyOptions EngineOptions;
		EngineOptions.AngleThreshold = 5.0f;
		UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPlanar(Engine, EngineOptions);

		FDynamicMesh3 Ours = Case.Mesh;
		PCGUtilsDynMeshDecimate::FPlanarOptions Options;
		Options.bDelimitUVSeams = false;
		PCGUtilsDynMeshDecimate::DissolvePlanar(Ours, Options);

		AddInfo(FString::Printf(TEXT("%s: source %d tris | Simplify Planar 5deg: %d tris, min angle %.1f | Decimate Planar 5deg: %d tris, min angle %.1f"),
			Case.Name, Case.Mesh.TriangleCount(), Engine->GetTriangleCount(), MinAngle(Engine->GetMeshRef()), Ours.TriangleCount(), MinAngle(Ours)));
		TestTrue(FString::Printf(TEXT("%s: Decimate Planar is valid"), Case.Name), IsValidMesh(Ours));
		TestEqual(FString::Printf(TEXT("%s: volume unchanged"), Case.Name), Volume(Ours), Volume(Case.Mesh), FMath::Abs(Volume(Case.Mesh)) * 1e-4 + 1e-3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDecimateCurvedTest, "PCGUtils.DynMesh.Decimate.CurvedInput", PCGUtilsDynMeshDecimateTests::Flags)
bool FPCGUtilsDecimateCurvedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshDecimateTests;
	// Box-topology sphere: a subdivided grid on each face, with poles at the eight cube corners.
	UDynamicMesh* SphereBox = NewObject<UDynamicMesh>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(SphereBox, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 9, 9, 9);
	FDynamicMesh3 Mesh = SphereBox->GetMeshRef();
	const int32 SourceTris = Mesh.TriangleCount();
	const PCGUtilsDynMeshDecimate::FResult Result = PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, 1);
	AddInfo(FString::Printf(TEXT("Sphere box: %d -> %d triangles, %d face centers, %d blocked"), SourceTris, Mesh.TriangleCount(), Result.GridCenters, Result.BlockedVertices));
	TestEqual(TEXT("Un-Subdivide quarters a box sphere"), Mesh.TriangleCount(), SourceTris / 4);
	TestTrue(TEXT("Un-Subdivided sphere is valid"), IsValidMesh(Mesh));
	TestTrue(TEXT("Un-Subdivided sphere stays closed"), Mesh.IsClosed());

	UDynamicMesh* LatLong = NewObject<UDynamicMesh>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(LatLong, FGeometryScriptPrimitiveOptions(), FTransform::Identity, 50, 16, 24);
	FDynamicMesh3 Planar = LatLong->GetMeshRef();
	PCGUtilsDynMeshDecimate::FPlanarOptions Options;
	Options.bDelimitUVSeams = false;
	Options.AngleLimitDegrees = 30.0;
	PCGUtilsDynMeshDecimate::DissolvePlanar(Planar, Options);
	AddInfo(FString::Printf(TEXT("Lat-long sphere Planar 30deg: %d -> %d triangles"), LatLong->GetTriangleCount(), Planar.TriangleCount()));
	TestTrue(TEXT("Planar 30 degrees reduces a sphere"), Planar.TriangleCount() < LatLong->GetTriangleCount());
	TestTrue(TEXT("Planar sphere is valid"), IsValidMesh(Planar));
	TestTrue(TEXT("Planar sphere stays closed"), Planar.IsClosed());
	return true;
}

#endif
