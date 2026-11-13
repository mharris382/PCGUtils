// Copyright Max Harris
//
// Voxel | To DynMesh: closed, manifold, outward meshes; collapse without cracks; colors, offsets and UVs.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/PCGVoxelToDynMesh.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelMeshTests
{
	using namespace PCGVoxelTestHelpers;

	UPCGVoxelToDynMeshSettings* MakeSettings()
	{
		UPCGVoxelToDynMeshSettings* Settings = NewObject<UPCGVoxelToDynMeshSettings>();
		Settings->bConvertToLocalSpace = false;
		return Settings;
	}

	TArray<FPCGTaggedData> Mesh(UPCGVoxelToDynMeshSettings* Settings, const UPCGData* Voxels)
	{
		return Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
	}

	/** Closed, every edge shared by exactly two triangles, faces outward, and encloses Cells cubes of 100. */
	void CheckSolid(FAutomationTestBase& Test, const TCHAR* What, const UE::Geometry::FDynamicMesh3& Mesh, int32 Cells, double Size = 100.0)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s: no open edges"), What), OpenEdges(Mesh), 0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the mesh is valid"), What), Mesh.CheckValidity(UE::Geometry::FDynamicMesh3::FValidityOptions(), UE::Geometry::EValidityCheckFailMode::ReturnOnly));
		const double Expected = Cells * Size * Size * Size;
		Test.TestTrue(*FString::Printf(TEXT("%s: encloses %.0f outward (%.0f)"), What, Expected, Volume(Mesh)),
			FMath::IsNearlyEqual(Volume(Mesh), Expected, Expected * 1e-6));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshSingleCellTest,
	"PCGUtils.Voxel.ToDynMesh.OneCellIsAClosedBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshSingleCellTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;
	const TArray<FPCGTaggedData> Outputs = Mesh(MakeSettings(), MakeVoxels({ FIntVector(2, 0, 0) }));
	const UE::Geometry::FDynamicMesh3* Result = MeshOf(Outputs);
	UTEST_NOT_NULL(TEXT("A DynMesh came out"), Result);
	TestEqual(TEXT("12 triangles"), Result->TriangleCount(), 12);
	TestEqual(TEXT("8 vertices"), Result->VertexCount(), 8);
	CheckSolid(*this, TEXT("One cell"), *Result, 1);
	const FBox Bounds(Result->GetBounds());
	TestTrue(*FString::Printf(TEXT("It spans the cell around (200, 0, 0) (%s)"), *Bounds.ToString()),
		Bounds.Min.Equals(FVector(150, -50, -50), 0.01) && Bounds.Max.Equals(FVector(250, 50, 50), 0.01));
	TestTrue(TEXT("The input's tags carry over"), Outputs[0].Tags.Contains(TEXT("KeptTag")));
	const UPCGDynamicMeshData* Data = Cast<UPCGDynamicMeshData>(Outputs[0].Data);
	TestTrue(TEXT("Its @Data.VoxelSize carries over"), Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data)
		&& Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data)->HasAttribute(TEXT("VoxelSize")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshManifoldTest,
	"PCGUtils.Voxel.ToDynMesh.CellsTouchingAtAnEdgeOrCornerStayManifold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshManifoldTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(MakeSettings(), MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 1, 0) })));
	UTEST_NOT_NULL(TEXT("Edge pair meshed"), Result);
	TestEqual(TEXT("Two cells touching along an edge get separate vertices there (16)"), Result->VertexCount(), 16);
	CheckSolid(*this, TEXT("Edge pair"), *Result, 2);

	Result = MeshOf(Mesh(MakeSettings(), MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 1, 1) })));
	UTEST_NOT_NULL(TEXT("Corner pair meshed"), Result);
	TestEqual(TEXT("Two cells touching at a corner get separate vertices there (16)"), Result->VertexCount(), 16);
	CheckSolid(*this, TEXT("Corner pair"), *Result, 2);

	// A 2x2x2 block missing two cells that touch along an edge: the empty side has the awkward edge.
	TArray<FIntVector> Cells = Block(2, 2, 2);
	Cells.Remove(FIntVector(0, 0, 0));
	Cells.Remove(FIntVector(1, 1, 0));
	Result = MeshOf(Mesh(MakeSettings(), MakeVoxels(Cells)));
	UTEST_NOT_NULL(TEXT("Notched block meshed"), Result);
	CheckSolid(*this, TEXT("Block missing an edge pair"), *Result, 6);

	// An L of three cells has a concave edge.
	Result = MeshOf(Mesh(MakeSettings(), MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) })));
	UTEST_NOT_NULL(TEXT("L meshed"), Result);
	CheckSolid(*this, TEXT("L"), *Result, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshCollapseTest,
	"PCGUtils.Voxel.ToDynMesh.CollapseLeavesNoCracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshCollapseTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	// A 5x4x3 block with a few cells missing, so merged faces meet uneven neighbours.
	TArray<FIntVector> Cells = Block(5, 4, 3);
	for (const FIntVector& Missing : { FIntVector(2, 1, 2), FIntVector(4, 3, 2), FIntVector(0, 0, 0), FIntVector(3, 2, 2), FIntVector(1, 3, 1) })
	{
		Cells.Remove(Missing);
	}
	const UPCGPointArrayData* Voxels = MakeVoxels(Cells);

	UPCGVoxelToDynMeshSettings* Plain = MakeSettings();
	const TArray<FPCGTaggedData> PlainOut = Mesh(Plain, Voxels);
	UPCGVoxelToDynMeshSettings* Collapsed = MakeSettings();
	Collapsed->bCollapseCoplanarFaces = true;
	const TArray<FPCGTaggedData> CollapsedOut = Mesh(Collapsed, Voxels);
	const UE::Geometry::FDynamicMesh3* PlainMesh = MeshOf(PlainOut);
	const UE::Geometry::FDynamicMesh3* CollapsedMesh = MeshOf(CollapsedOut);
	UTEST_TRUE(TEXT("Both meshed"), PlainMesh && CollapsedMesh);

	CheckSolid(*this, TEXT("Plain"), *PlainMesh, Cells.Num());
	CheckSolid(*this, TEXT("Collapsed"), *CollapsedMesh, Cells.Num());
	TestTrue(*FString::Printf(TEXT("Collapsing uses fewer triangles (%d vs %d)"), CollapsedMesh->TriangleCount(), PlainMesh->TriangleCount()),
		CollapsedMesh->TriangleCount() * 4 < PlainMesh->TriangleCount() * 3);

	// A plain box collapses to the minimum: six faces of two triangles.
	const UE::Geometry::FDynamicMesh3* Box = MeshOf(Mesh(Collapsed, MakeVoxels(Block(4, 3, 2))));
	UTEST_NOT_NULL(TEXT("Box meshed"), Box);
	TestEqual(TEXT("A collapsed box is 12 triangles"), Box->TriangleCount(), 12);
	CheckSolid(*this, TEXT("Collapsed box"), *Box, 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshColorTest,
	"PCGUtils.Voxel.ToDynMesh.HardAndSmoothColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshColorTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
	auto Colors = Voxels->GetColorValueRange();
	Colors[0] = FVector4(1, 0, 0, 1);
	Colors[1] = FVector4(0, 0, 1, 1);

	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	Settings->VertexColorSource = EPCGVoxelColorSource::Attribute;
	const UE::Geometry::FDynamicMesh3* Hard = MeshOf(Mesh(Settings, Voxels));
	UTEST_TRUE(TEXT("Hard meshed with colors"), Hard && Hard->HasAttributes() && Hard->Attributes()->HasPrimaryColors());
	bool bAllPure = true;
	const UE::Geometry::FDynamicMeshColorOverlay* HardColors = Hard->Attributes()->PrimaryColors();
	for (const int32 Element : HardColors->ElementIndicesItr())
	{
		const FVector4f Color = HardColors->GetElement(Element);
		bAllPure &= Color.Equals(FVector4f(1, 0, 0, 1)) || Color.Equals(FVector4f(0, 0, 1, 1));
	}
	TestTrue(TEXT("Hard: every color is one cell's exact color"), bAllPure);
	CheckSolid(*this, TEXT("Hard colors"), *Hard, 2);

	Settings->ColorTransition = EPCGVoxelColorTransition::Smooth;
	const UE::Geometry::FDynamicMesh3* Smooth = MeshOf(Mesh(Settings, Voxels));
	UTEST_TRUE(TEXT("Smooth meshed"), Smooth && Smooth->Attributes()->HasPrimaryColors());
	bool bFoundBlend = false;
	const UE::Geometry::FDynamicMeshColorOverlay* SmoothColors = Smooth->Attributes()->PrimaryColors();
	for (const int32 Element : SmoothColors->ElementIndicesItr())
	{
		bFoundBlend |= SmoothColors->GetElement(Element).Equals(FVector4f(0.5f, 0, 0.5f, 1), 1e-4f);
	}
	TestTrue(TEXT("Smooth: the corners the two cells share are their average"), bFoundBlend);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshOffsetTest,
	"PCGUtils.Voxel.ToDynMesh.SurfaceOffsetMovesOnlyTheSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshOffsetTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	// A 2x1x1 bar whose second cell's bounds reach 20 further up and 10 further out in +X.
	UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
	Voxels->GetBoundsMaxValueRange()[1] = FVector(60, 50, 70);
	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	Settings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;
	Settings->OffsetBlend = EPCGVoxelOffsetBlend::Average;
	const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, Voxels));
	UTEST_NOT_NULL(TEXT("Meshed"), Result);
	TestEqual(TEXT("Still closed"), OpenEdges(*Result), 0);
	TestEqual(TEXT("Same topology as without the offset (12 vertices)"), Result->VertexCount(), 12);
	const FBox Bounds(Result->GetBounds());
	TestTrue(*FString::Printf(TEXT("The +X face moved out 10 and the top reached 70 (%s)"), *Bounds.ToString()),
		FMath::IsNearlyEqual(Bounds.Max.X, 160.0, 0.01) && FMath::IsNearlyEqual(Bounds.Max.Z, 70.0, 0.01));
	TestTrue(TEXT("The first cell's far side did not move"), FMath::IsNearlyEqual(Bounds.Min.X, -50.0, 0.01));

	// A Constant inset deep enough to cross opposite faces (Y and Z here, both sides showing) is repaired, not refused.
	Settings->SurfaceOffset = EPCGVoxelSurfaceOffset::Constant;
	Settings->SurfaceOffsetDistance.Constant = -60.0;
	AddExpectedMessage(TEXT("2 of 2 points have a surface offset that crosses their own opposite face"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, false);
	const UE::Geometry::FDynamicMesh3* Repaired = MeshOf(Mesh(Settings, Voxels));
	UTEST_NOT_NULL(TEXT("Too deep an inset still meshes"), Repaired);
	const FBox RepairedBounds(Repaired->GetBounds());
	TestTrue(*FString::Printf(TEXT("Repaired points collapse to a thin slab in Y (%s)"), *RepairedBounds.ToString()),
		RepairedBounds.Max.Y - RepairedBounds.Min.Y <= 0.11 && RepairedBounds.Max.Y >= RepairedBounds.Min.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshExactOffsetTest,
	"PCGUtils.Voxel.ToDynMesh.ExactOffsetsMatchPointBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshExactOffsetTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;
	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	Settings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;
	TestTrue(TEXT("Exact is the default"), Settings->OffsetBlend == EPCGVoxelOffsetBlend::Exact);

	// The bar from the averaging test: the second box is 110 x 100 x 120, the first stays a cube with a flat top.
	{
		UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
		Voxels->GetBoundsMaxValueRange()[1] = FVector(60, 50, 70);
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, Voxels));
		UTEST_NOT_NULL(TEXT("Bar meshed"), Result);
		TestEqual(TEXT("Exact bar: no open edges"), OpenEdges(*Result), 0);
		TestTrue(TEXT("Exact bar: the mesh is valid"), Result->CheckValidity(UE::Geometry::FDynamicMesh3::FValidityOptions(), UE::Geometry::EValidityCheckFailMode::ReturnOnly));
		TestTrue(*FString::Printf(TEXT("Exact bar: encloses 1000000 + 110 x 100 x 120 (%.0f)"), Volume(*Result)),
			FMath::IsNearlyEqual(Volume(*Result), 2320000.0, 2.32));
		double FirstTop = -1e9;
		for (const int32 VertexID : Result->VertexIndicesItr())
		{
			const FVector3d Vertex = Result->GetVertex(VertexID);
			if (Vertex.X < 49.0)
			{
				FirstTop = FMath::Max(FirstTop, Vertex.Z);
			}
		}
		TestTrue(*FString::Printf(TEXT("The first cell's top stays flat at 50 (%.3f)"), FirstTop), FMath::IsNearlyEqual(FirstTop, 50.0, 0.01));
	}

	// Columns of different heights round a shared corner (2 x 3), and a two-cell column beside a one-cell one (the
	// Best Fit shape): every mesh is closed and encloses exactly the union of the point boxes.
	struct FCase { const TCHAR* Name; TArray<FIntVector> Cells; TArray<double> MaxZ; };
	const TArray<FCase> Cases = {
		{ TEXT("Six columns"), { {0,0,0}, {1,0,0}, {2,0,0}, {0,1,0}, {1,1,0}, {2,1,0} }, { 10, 30, -20, 40, 0, 20 } },
		{ TEXT("Step under a column"), { {0,0,0}, {0,0,1}, {1,0,0} }, { 50, -20, 30 } },
		{ TEXT("Three heights at one corner"), { {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0} }, { 10, 20, 30, 50 } },
	};
	for (const FCase& Case : Cases)
	{
		for (const bool bCollapse : { false, true })
		{
			UPCGPointArrayData* Voxels = MakeVoxels(Case.Cells);
			double Expected = 0.0;
			for (int32 Index = 0; Index < Case.Cells.Num(); ++Index)
			{
				Voxels->GetBoundsMaxValueRange()[Index].Z = Case.MaxZ[Index];
				Expected += 100.0 * 100.0 * (Case.MaxZ[Index] + 50.0);
			}
			Settings->bCollapseCoplanarFaces = bCollapse;
			Settings->VertexColorSource = EPCGVoxelColorSource::Attribute;
			const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, Voxels));
			const FString What = FString::Printf(TEXT("%s%s"), Case.Name, bCollapse ? TEXT(" (collapsed)") : TEXT(""));
			UTEST_NOT_NULL(*What, Result);
			TestEqual(*FString::Printf(TEXT("%s: no open edges"), *What), OpenEdges(*Result), 0);
			TestTrue(*FString::Printf(TEXT("%s: the mesh is valid"), *What), Result->CheckValidity(UE::Geometry::FDynamicMesh3::FValidityOptions(), UE::Geometry::EValidityCheckFailMode::ReturnOnly));
			TestTrue(*FString::Printf(TEXT("%s: encloses the point boxes, %.0f (%.0f)"), *What, Expected, Volume(*Result)),
				FMath::IsNearlyEqual(Volume(*Result), Expected, Expected * 1e-6));
			bool bNormalsSet = true;
			const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Result->Attributes()->PrimaryNormals();
			for (const int32 TriangleID : Result->TriangleIndicesItr())
			{
				const UE::Geometry::FIndex3i Elements = Normals->GetTriangle(TriangleID);
				for (int32 K = 0; K < 3; ++K)
				{
					bNormalsSet &= Elements[K] >= 0 && FMath::IsNearlyEqual(Normals->GetElement(Elements[K]).Size(), 1.0f, 1e-3f);
				}
			}
			TestTrue(*FString::Printf(TEXT("%s: every triangle has unit normals"), *What), bNormalsSet);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshOffsetPastCentreTest,
	"PCGUtils.Voxel.ToDynMesh.SurfaceOffsetPastCentreIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshOffsetPastCentreTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;
	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	Settings->SurfaceOffset = EPCGVoxelSurfaceOffset::FromPointBounds;

	// Bounds Max Z 0, Min Z -50: the top face is at the centre, but the cell is a valid 50-unit slab. No warning.
	{
		UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
		Voxels->GetBoundsMaxValueRange()[0].Z = 0.0;
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, Voxels));
		UTEST_NOT_NULL(TEXT("A top face at the centre meshes"), Result);
		TestEqual(TEXT("Still closed"), OpenEdges(*Result), 0);
		const FBox Bounds(Result->GetBounds());
		TestTrue(*FString::Printf(TEXT("The slab spans -50 to 50 overall (%s)"), *Bounds.ToString()),
			FMath::IsNearlyEqual(Bounds.Min.Z, -50.0, 0.01) && FMath::IsNearlyEqual(Bounds.Max.Z, 50.0, 0.01));
	}
	// Bounds Max Z -30 under Min Z 10 on one cell of many: that cell is repaired, the rest mesh as given.
	{
		UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(3, 0, 0), FIntVector(6, 0, 0) });
		Voxels->GetBoundsMaxValueRange()[1].Z = -30.0;
		Voxels->GetBoundsMinValueRange()[1].Z = 10.0;
		AddExpectedMessage(TEXT("thick (Offset Repair). point 1 on Z: its +Z face at -30 and -Z face at 10"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1, false);
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, Voxels));
		UTEST_NOT_NULL(TEXT("One inverted cell does not stop the set"), Result);
		TestEqual(TEXT("Still closed"), OpenEdges(*Result), 0);
		TestEqual(TEXT("All three cells meshed (24 vertices)"), Result->VertexCount(), 24);
		double RepairedTop = -1e9;
		double RepairedBottom = 1e9;
		for (const int32 VertexID : Result->VertexIndicesItr())
		{
			const FVector3d Vertex = Result->GetVertex(VertexID);
			if (Vertex.X > 240.0 && Vertex.X < 360.0)
			{
				RepairedTop = FMath::Max(RepairedTop, Vertex.Z);
				RepairedBottom = FMath::Min(RepairedBottom, Vertex.Z);
			}
		}
		TestTrue(*FString::Printf(TEXT("The repaired cell is a thin slab at Z -10 (%.3f to %.3f)"), RepairedBottom, RepairedTop),
			RepairedBottom < RepairedTop && FMath::IsNearlyEqual(0.5 * (RepairedTop + RepairedBottom), -10.0, 0.01) && RepairedTop - RepairedBottom <= 0.11);
	}
	// The other repairs on the same inverted cell.
	auto InvertedSet = []()
	{
		UPCGPointArrayData* Voxels = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(3, 0, 0), FIntVector(6, 0, 0) });
		Voxels->GetBoundsMaxValueRange()[1].Z = -30.0;
		Voxels->GetBoundsMinValueRange()[1].Z = 10.0;
		return Voxels;
	};
	{
		Settings->OffsetRepair = EPCGVoxelOffsetRepair::ZeroThickness;
		AddExpectedMessage(TEXT("collapsed to zero thickness"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, InvertedSet()));
		UTEST_NOT_NULL(TEXT("Zero Thickness meshes"), Result);
		TestEqual(TEXT("Zero Thickness: still closed"), OpenEdges(*Result), 0);
		TestEqual(TEXT("Zero Thickness: all three cells (24 vertices)"), Result->VertexCount(), 24);
		bool bFlat = true;
		for (const int32 VertexID : Result->VertexIndicesItr())
		{
			const FVector3d Vertex = Result->GetVertex(VertexID);
			if (Vertex.X > 240.0 && Vertex.X < 360.0)
			{
				bFlat &= FMath::IsNearlyEqual(Vertex.Z, -10.0, 1e-6);
			}
		}
		TestTrue(TEXT("Zero Thickness: the repaired cell lies flat at Z -10"), bFlat);
	}
	{
		Settings->OffsetRepair = EPCGVoxelOffsetRepair::OneSidedPlane;
		AddExpectedMessage(TEXT("turned into a one-sided plane"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, InvertedSet()));
		UTEST_NOT_NULL(TEXT("One-Sided Plane meshes"), Result);
		TestEqual(TEXT("One-Sided Plane: two cubes and one quad (8 + 8 + 4 vertices)"), Result->VertexCount(), 20);
		TestEqual(TEXT("One-Sided Plane: 12 + 12 + 2 triangles"), Result->TriangleCount(), 26);
		TestEqual(TEXT("One-Sided Plane: the quad's 4 edges are open"), OpenEdges(*Result), 4);
		// Normal sign follows the engine's winding, so compare with a top face of an untouched cube.
		double PlaneNormalZ = 0.0;
		double TopNormalZ = 0.0;
		for (const int32 TriangleID : Result->TriangleIndicesItr())
		{
			const FVector3d Centroid = Result->GetTriCentroid(TriangleID);
			if (Centroid.X > 240.0 && Centroid.X < 360.0)
			{
				PlaneNormalZ = Result->GetTriNormal(TriangleID).Z;
			}
			else if (Centroid.X < 60.0 && FMath::IsNearlyEqual(Centroid.Z, 50.0, 0.01))
			{
				TopNormalZ = Result->GetTriNormal(TriangleID).Z;
			}
		}
		const bool bFacesUp = FMath::Abs(PlaneNormalZ) > 0.99 && PlaneNormalZ * TopNormalZ > 0.0;
		TestTrue(TEXT("One-Sided Plane: the kept face is the +Z one"), bFacesUp);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshUVTest,
	"PCGUtils.Voxel.ToDynMesh.GridUVsHaveNoSeamsWithinAPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshUVTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(MakeSettings(), MakeVoxels(Block(3, 2, 1))));
	UTEST_TRUE(TEXT("Meshed with UVs"), Result && Result->HasAttributes() && Result->Attributes()->NumUVLayers() > 0);
	const UE::Geometry::FDynamicMeshUVOverlay* UVs = Result->Attributes()->GetUVLayer(0);
	int32 FlatEdges = 0;
	int32 FlatSeams = 0;
	for (const int32 EdgeID : Result->EdgeIndicesItr())
	{
		const UE::Geometry::FIndex2i Tris = Result->GetEdgeT(EdgeID);
		if (Tris.B < 0)
		{
			continue;
		}
		if (Result->GetTriNormal(Tris.A).Dot(Result->GetTriNormal(Tris.B)) > 0.999)
		{
			++FlatEdges;
			FlatSeams += UVs->IsSeamEdge(EdgeID) ? 1 : 0;
		}
	}
	TestTrue(TEXT("There are edges inside flat faces"), FlatEdges > 0);
	TestEqual(TEXT("No UV seam inside a flat face, across cells"), FlatSeams, 0);

	// The top of cell (2, 1, 0) continues the grid: 1 UV unit per cell.
	FVector2f MaxUV(-1e9f);
	for (const int32 Element : UVs->ElementIndicesItr())
	{
		MaxUV = FVector2f(FMath::Max(MaxUV.X, UVs->GetElement(Element).X), FMath::Max(MaxUV.Y, UVs->GetElement(Element).Y));
	}
	TestTrue(*FString::Printf(TEXT("UVs run one unit per cell (largest U %.2f)"), MaxUV.X), FMath::IsNearlyEqual(MaxUV.X, 2.5f, 1e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshSparseTest,
	"PCGUtils.Voxel.ToDynMesh.FarApartCellsUseTheHashMapAndMeshTheSame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshSparseTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;

	// A 3x3x2 block and a 2-cell brick 200000 cells away: the bounding box is far too big for the dense lookup.
	TArray<FIntVector> Cells = Block(3, 3, 2);
	Cells.Add(FIntVector(200000, 0, 0));
	Cells.Add(FIntVector(200001, 0, 0));

	FPCGVoxelGrid Grid;
	Grid.PointFirst = Cells;
	UTEST_TRUE(TEXT("The cells are read"), Grid.BuildCells());
	TestFalse(TEXT("A set spread this far uses the hash map"), Grid.IsDense());
	TestEqual(TEXT("A far cell is found"), Grid.FindPoint(FIntVector(200001, 0, 0)), 19);
	TestEqual(TEXT("An empty cell between them is not"), Grid.FindPoint(FIntVector(1000, 0, 0)), INDEX_NONE);
	TestEqual(TEXT("Cells stay ordered Z, Y, X"), Grid.Cells[3], FIntVector(200000, 0, 0));

	FPCGVoxelGrid Near;
	Near.PointFirst = Block(3, 3, 2);
	Near.BuildCells();
	TestTrue(TEXT("The block alone uses the dense lookup"), Near.IsDense());

	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	const UE::Geometry::FDynamicMesh3* Plain = MeshOf(Mesh(Settings, MakeVoxels(Cells)));
	UTEST_NOT_NULL(TEXT("Plain meshed"), Plain);
	CheckSolid(*this, TEXT("Sparse plain"), *Plain, 20);
	TestEqual(TEXT("Plain: two triangles per exposed face (block 42 faces, brick 10)"), Plain->TriangleCount(), (42 + 10) * 2);

	Settings->bCollapseCoplanarFaces = true;
	const UE::Geometry::FDynamicMesh3* Collapsed = MeshOf(Mesh(Settings, MakeVoxels(Cells)));
	UTEST_NOT_NULL(TEXT("Collapsed meshed"), Collapsed);
	CheckSolid(*this, TEXT("Sparse collapsed"), *Collapsed, 20);
	TestEqual(TEXT("Collapsed: each box is 12 triangles"), Collapsed->TriangleCount(), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMeshPolygroupAttributeTest,
	"PCGUtils.Voxel.ToDynMesh.PolygroupsPerAttributeValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelMeshPolygroupAttributeTest::RunTest(const FString&)
{
	using namespace PCGVoxelMeshTests;
	// A row of four cells: cells 0 and 1 share one value, cells 2 and 3 another. The seam between them is at x = 150.
	UPCGPointArrayData* Row = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0), FIntVector(3, 0, 0) });
	FPCGMetadataDomain* Elements = Row->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	FPCGMetadataAttribute<FString>* Name = Elements->CreateAttribute<FString>(TEXT("Room"), FString(), false);
	FPCGMetadataAttribute<int32>* Number = Elements->CreateAttribute<int32>(TEXT("Polygroup"), 0, false);
	const TConstPCGValueRange<int64> Keys = Row->GetConstMetadataEntryValueRange();
	const TCHAR* Names[4] = { TEXT("b"), TEXT("b"), TEXT("a"), TEXT("a") };
	const int32 Numbers[4] = { 7, 7, 3, 3 };
	for (int32 I = 0; I < 4; ++I)
	{
		Name->SetValue(Keys[I], FString(Names[I]));
		Number->SetValue(Keys[I], Numbers[I]);
	}

	// Each triangle's group must be the one of the side of the seam it lies on, and collapsed faces must not cross it.
	auto Check = [this](const FString& What, const UE::Geometry::FDynamicMesh3* Result, int32 LeftGroup, int32 RightGroup)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s: a mesh came out"), *What), Result))
		{
			return;
		}
		TestEqual(*FString::Printf(TEXT("%s: closed"), *What), OpenEdges(*Result), 0);
		int32 Wrong = 0;
		int32 Crossing = 0;
		for (const int32 Tri : Result->TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i V = Result->GetTriangle(Tri);
			double MinX = TNumericLimits<double>::Max();
			double MaxX = TNumericLimits<double>::Lowest();
			for (int32 C = 0; C < 3; ++C)
			{
				MinX = FMath::Min(MinX, Result->GetVertex(V[C]).X);
				MaxX = FMath::Max(MaxX, Result->GetVertex(V[C]).X);
			}
			if (MinX < 149.0 && MaxX > 151.0)
			{
				++Crossing;
			}
			const int32 Expected = (MinX + MaxX) * 0.5 < 150.0 ? LeftGroup : RightGroup;
			Wrong += Result->GetTriangleGroup(Tri) != Expected ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("%s: no triangle crosses the seam between values"), *What), Crossing, 0);
		TestEqual(*FString::Printf(TEXT("%s: every triangle has its cell's group"), *What), Wrong, 0);
	};

	// Strings: numbered in order of first point, so "b" is 0 and "a" is 1. Collapse on.
	UPCGVoxelToDynMeshSettings* Settings = MakeSettings();
	Settings->Polygroups = EPCGVoxelPolygroupMode::PerAttributeValue;
	Settings->PolygroupAttribute.SetAttributeName(TEXT("Room"));
	Settings->bCollapseCoplanarFaces = true;
	Check(TEXT("String values, collapsed"), MeshOf(Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } })), 0, 1);

	// Integers are the IDs themselves (default attribute name Polygroup).
	Settings = MakeSettings();
	Settings->Polygroups = EPCGVoxelPolygroupMode::PerAttributeValue;
	Settings->bCollapseCoplanarFaces = true;
	Check(TEXT("Integer values as IDs"), MeshOf(Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } })), 7, 3);

	// Or numbered like any other value.
	Settings->bIntegerValuesAreGroupIds = false;
	Check(TEXT("Integer values numbered"), MeshOf(Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } })), 0, 1);

	// Bevel mode: bands and corners take one neighbouring cell's group; the mesh stays closed.
	Settings = MakeSettings();
	Settings->Polygroups = EPCGVoxelPolygroupMode::PerAttributeValue;
	Settings->PolygroupAttribute.SetAttributeName(TEXT("Room"));
	Settings->Shape = EPCGVoxelMeshShape::Bevel;
	Settings->BevelAmount = 0.25f;
	if (const UE::Geometry::FDynamicMesh3* Beveled = MeshOf(Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } })))
	{
		TestEqual(TEXT("Bevel: closed"), OpenEdges(*Beveled), 0);
		TSet<int32> Groups;
		for (const int32 Tri : Beveled->TriangleIndicesItr())
		{
			Groups.Add(Beveled->GetTriangleGroup(Tri));
		}
		TestEqual(TEXT("Bevel: two groups"), Groups.Num(), 2);
	}
	else
	{
		AddError(TEXT("Bevel: no mesh came out"));
	}

	// Errors.
	Number->SetValue(Keys[2], -4);
	Settings = MakeSettings();
	Settings->Polygroups = EPCGVoxelPolygroupMode::PerAttributeValue;
	AddExpectedError(TEXT("point 2: Polygroup Attribute 'Polygroup' is -4; with Integer Values Are Group IDs on, a group ID must be from 0 to"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("A negative group ID makes no mesh"), Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } }).Num(), 0);

	Settings->PolygroupAttribute.SetAttributeName(TEXT("Missing"));
	AddExpectedError(TEXT("Polygroup Attribute 'Missing' does not exist on the points; Polygroups is Per Attribute Value, which needs it."),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("A missing attribute makes no mesh"), Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Row } }).Num(), 0);
	return true;
}

#endif
