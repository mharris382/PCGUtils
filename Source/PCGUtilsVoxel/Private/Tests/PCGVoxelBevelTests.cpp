// Copyright Max Harris
//
// Voxel | To DynMesh, Shape Bevel: exact volumes on shapes worked out by hand, each control touching only its own
// edges, and closed valid meshes on random lumpy shapes.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelToDynMesh.h"
#include "Math/RandomStream.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelBevelTests
{
	using namespace PCGVoxelTestHelpers;

	UPCGVoxelToDynMeshSettings* MakeBevel(float Amount)
	{
		UPCGVoxelToDynMeshSettings* Settings = NewObject<UPCGVoxelToDynMeshSettings>();
		Settings->bConvertToLocalSpace = false;
		Settings->Shape = EPCGVoxelMeshShape::Bevel;
		Settings->BevelAmount = Amount;
		return Settings;
	}

	UPCGVoxelToDynMeshSettings* MakeFull()
	{
		UPCGVoxelToDynMeshSettings* Settings = MakeBevel(0.0f);
		Settings->BevelControls = EPCGVoxelBevelControls::Full;
		Settings->ConvexTopBevel = Settings->ConvexSideBevel = Settings->ConvexBottomBevel = 0.0f;
		Settings->ConcaveTopBevel = Settings->ConcaveSideBevel = Settings->ConcaveBottomBevel = 0.0f;
		return Settings;
	}

	TArray<FPCGTaggedData> Mesh(UPCGVoxelToDynMeshSettings* Settings, const UPCGData* Voxels)
	{
		return Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
	}

	/** Closed, valid, and encloses Expected cubic cells (of 100). Returns the volume in cells. */
	double CheckClosed(FAutomationTestBase& Test, const FString& What, const UE::Geometry::FDynamicMesh3* Result)
	{
		if (!Result)
		{
			Test.AddError(FString::Printf(TEXT("%s: no mesh came out"), *What));
			return 0.0;
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: no open edges"), *What), OpenEdges(*Result), 0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the mesh is valid"), *What),
			Result->CheckValidity(UE::Geometry::FDynamicMesh3::FValidityOptions(), UE::Geometry::EValidityCheckFailMode::ReturnOnly));
		return Volume(*Result) / 1e6;
	}

	void CheckVolume(FAutomationTestBase& Test, const FString& What, double Got, double Expected)
	{
		Test.TestTrue(*FString::Printf(TEXT("%s: encloses %.6f cells (got %.6f)"), *What, Expected, Got), FMath::IsNearlyEqual(Got, Expected, 1e-6));
	}

	/** A random lumpy shape in a Size^3 box, with holes, notches and cells touching only along edges or at corners. */
	TArray<FIntVector> Lumpy(int32 Seed, int32 Size, float Fill)
	{
		FRandomStream Random(Seed);
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < Size; ++Z)
			for (int32 Y = 0; Y < Size; ++Y)
				for (int32 X = 0; X < Size; ++X)
					if (Random.FRand() < Fill)
						Cells.Add(FIntVector(X, Y, Z));
		return Cells;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBevelCubeTest,
	"PCGUtils.Voxel.Bevel.ACubeIsChamferedExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBevelCubeTest::RunTest(const FString&)
{
	using namespace PCGVoxelBevelTests;
	const UPCGPointArrayData* Cube = MakeVoxels({ FIntVector(3, -1, 2) });

	// Chamfer a: each edge loses a prism a^2/2 long 1 - 2a; each corner loses 5a^3/6.
	for (const float A : { 0.1f, 0.25f, 0.4f })
	{
		const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(MakeBevel(A), Cube));
		const double Got = CheckClosed(*this, FString::Printf(TEXT("Cube at %.2f"), A), Result);
		CheckVolume(*this, FString::Printf(TEXT("Cube at %.2f"), A), Got, 1.0 - 6.0 * A * A + 16.0 / 3.0 * A * A * A);
		if (Result)
		{
			TestEqual(*FString::Printf(TEXT("Cube at %.2f: 6 faces, 12 strips, 8 corners make 44 triangles"), A), Result->TriangleCount(), 44);
		}
	}

	// At 1 every face has two beveled edges opposite each other, so each pair is scaled to meet in the middle:
	// a = 0.5, which leaves the octahedron through the face centres.
	const double Full = CheckClosed(*this, TEXT("Cube at 1"), MeshOf(Mesh(MakeBevel(1.0f), Cube)));
	CheckVolume(*this, TEXT("Cube at 1 is an octahedron"), Full, 1.0 / 6.0);

	// 0 is the plain cube.
	CheckVolume(*this, TEXT("Cube at 0"), CheckClosed(*this, TEXT("Cube at 0"), MeshOf(Mesh(MakeBevel(0.0f), Cube))), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBevelControlsTest,
	"PCGUtils.Voxel.Bevel.EachControlTouchesOnlyItsEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBevelControlsTest::RunTest(const FString&)
{
	using namespace PCGVoxelBevelTests;
	constexpr double A = 0.25;
	const UPCGPointArrayData* Cube = MakeVoxels({ FIntVector(0, 0, 0) });
	const double Frustum = (1.0 - A) + A * (1.0 + (1.0 - 2.0 * A) * (1.0 - 2.0 * A) + (1.0 - 2.0 * A)) / 3.0;

	// Only the top ring of a cube: a frustum of height a on top of a box.
	UPCGVoxelToDynMeshSettings* Settings = MakeFull();
	Settings->ConvexTopBevel = A;
	CheckVolume(*this, TEXT("Convex Top on a cube"), CheckClosed(*this, TEXT("Convex Top on a cube"), MeshOf(Mesh(Settings, Cube))), Frustum);

	Settings = MakeFull();
	Settings->ConvexBottomBevel = A;
	CheckVolume(*this, TEXT("Convex Bottom on a cube"), CheckClosed(*this, TEXT("Convex Bottom on a cube"), MeshOf(Mesh(Settings, Cube))), Frustum);

	// Only the four vertical edges: an octagonal prism.
	Settings = MakeFull();
	Settings->ConvexSideBevel = A;
	CheckVolume(*this, TEXT("Convex Side on a cube"), CheckClosed(*this, TEXT("Convex Side on a cube"), MeshOf(Mesh(Settings, Cube))), 1.0 - 2.0 * A * A);

	// Concave controls leave a cube alone.
	Settings = MakeFull();
	Settings->ConcaveTopBevel = Settings->ConcaveSideBevel = Settings->ConcaveBottomBevel = 1.0f;
	CheckVolume(*this, TEXT("Concave only on a cube"), CheckClosed(*this, TEXT("Concave only on a cube"), MeshOf(Mesh(Settings, Cube))), 1.0);

	// An L of three cells has one vertical concave edge; filling it adds a prism a^2/2 tall 1.
	const UPCGPointArrayData* L = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) });
	Settings = MakeBevel(0.0f);
	Settings->BevelControls = EPCGVoxelBevelControls::ConvexConcave;
	Settings->ConvexBevel = 0.0f;
	Settings->ConcaveBevel = 0.5f;
	CheckVolume(*this, TEXT("Concave on an L"), CheckClosed(*this, TEXT("Concave on an L"), MeshOf(Mesh(Settings, L))), 3.0 + 0.125);

	Settings = MakeFull();
	Settings->ConcaveTopBevel = Settings->ConcaveBottomBevel = 1.0f;
	CheckVolume(*this, TEXT("Concave Top / Bottom skip a vertical edge"), CheckClosed(*this, TEXT("Concave Top / Bottom on an L"), MeshOf(Mesh(Settings, L))), 3.0);

	// A step (one cell on top of the end of a row of two) has one concave edge with an upward face.
	const UPCGPointArrayData* Step = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(1, 0, 1) });
	Settings = MakeFull();
	Settings->ConcaveTopBevel = 0.5f;
	CheckVolume(*this, TEXT("Concave Top on a step"), CheckClosed(*this, TEXT("Concave Top on a step"), MeshOf(Mesh(Settings, Step))), 3.0 + 0.125);
	Settings = MakeFull();
	Settings->ConcaveSideBevel = Settings->ConcaveBottomBevel = 1.0f;
	CheckVolume(*this, TEXT("Concave Side / Bottom leave a step"), CheckClosed(*this, TEXT("Concave Side / Bottom on a step"), MeshOf(Mesh(Settings, Step))), 3.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBevelScaleTest,
	"PCGUtils.Voxel.Bevel.ScaleAttributeAndErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBevelScaleTest::RunTest(const FString&)
{
	using namespace PCGVoxelBevelTests;
	auto WithScale = [](const TArray<FIntVector>& Cells, TFunctionRef<float(int32)> Scale)
	{
		UPCGPointArrayData* Points = MakeVoxels(Cells);
		FPCGMetadataDomain* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<float>* Attribute = Elements->CreateAttribute<float>(TEXT("Chamfer"), 1.0f, true);
		const TConstPCGValueRange<int64> Keys = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Attribute->SetValue(Keys[Index], Scale(Index));
		}
		return Points;
	};
	auto Settings = [](float Amount)
	{
		UPCGVoxelToDynMeshSettings* Result = MakeBevel(Amount);
		Result->bUseBevelScaleAttribute = true;
		Result->BevelScaleAttribute = TEXT("Chamfer");
		return Result;
	};

	// A scale of 0.5 on a bevel of 0.5 is a bevel of 0.25.
	const double Half = CheckClosed(*this, TEXT("Scale 0.5"), MeshOf(Mesh(Settings(0.5f), WithScale({ FIntVector(0) }, [](int32) { return 0.5f; }))));
	CheckVolume(*this, TEXT("Scale 0.5"), Half, 1.0 - 6.0 * 0.0625 + 16.0 / 3.0 * 0.015625);

	// Two cells in a row, one scaled to 0: its edges stay square and the band tapers into it; still closed.
	const UPCGPointArrayData* Mixed = WithScale({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) }, [](int32 Index) { return Index == 0 ? 1.0f : 0.0f; });
	const double MixedVolume = CheckClosed(*this, TEXT("Mixed scales"), MeshOf(Mesh(Settings(0.5f), Mixed)));
	TestTrue(*FString::Printf(TEXT("Mixed scales cut less than both cells beveled and something (%.4f)"), MixedVolume), MixedVolume > 2.0 * 0.5 && MixedVolume < 2.0 - 1e-3);

	// Errors: a negative scale, a missing attribute, an amount out of range. Each gives no mesh.
	AddExpectedError(TEXT("point 1: 'Chamfer' is -1; a bevel scale must be 0 or greater."), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("A negative scale gives no mesh"), Mesh(Settings(0.5f), WithScale({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) }, [](int32 Index) { return Index == 1 ? -1.0f : 1.0f; })).Num(), 0);

	AddExpectedError(TEXT("Bevel Scale Attribute 'Chamfer' does not exist on the points."), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("A missing scale attribute gives no mesh"), Mesh(Settings(0.5f), MakeVoxels({ FIntVector(0) })).Num(), 0);

	UPCGVoxelToDynMeshSettings* Bad = MakeFull();
	Bad->ConcaveSideBevel = 1.5f;
	AddExpectedError(TEXT("Concave Side Bevel is 1.5; it must be from 0 to 1 (a fraction of a cell)."), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestEqual(TEXT("An amount out of range gives no mesh"), Mesh(Bad, MakeVoxels({ FIntVector(0) })).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBevelLumpyTest,
	"PCGUtils.Voxel.Bevel.LumpyShapesStayClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBevelLumpyTest::RunTest(const FString&)
{
	using namespace PCGVoxelBevelTests;

	// Cells touching only along an edge or at a corner.
	for (const TArray<FIntVector>& Pair : { TArray<FIntVector>{ FIntVector(0, 0, 0), FIntVector(1, 1, 0) }, TArray<FIntVector>{ FIntVector(0, 0, 0), FIntVector(1, 1, 1) } })
	{
		const double Got = CheckClosed(*this, TEXT("Touching pair at 1"), MeshOf(Mesh(MakeBevel(1.0f), MakeVoxels(Pair))));
		CheckVolume(*this, TEXT("Touching pair at 1: two octahedra"), Got, 2.0 / 6.0);
	}

	for (int32 Seed = 0; Seed < 8; ++Seed)
	{
		const TArray<FIntVector> Cells = Lumpy(100 + Seed, 6, Seed % 2 == 0 ? 0.55f : 0.75f);
		const UPCGPointArrayData* Voxels = MakeVoxels(Cells);
		for (const float Amount : { 0.3f, 0.5f, 1.0f })
		{
			const FString What = FString::Printf(TEXT("Shape %d (%d cells) at %.1f"), Seed, Cells.Num(), Amount);
			const double Got = CheckClosed(*this, What, MeshOf(Mesh(MakeBevel(Amount), Voxels)));
			TestTrue(*FString::Printf(TEXT("%s: encloses a positive volume (%.3f)"), *What, Got), Got > 0.0);
		}
		UPCGVoxelToDynMeshSettings* Convex = MakeBevel(0.0f);
		Convex->BevelControls = EPCGVoxelBevelControls::ConvexConcave;
		Convex->ConvexBevel = 0.6f;
		Convex->ConcaveBevel = 0.0f;
		const double Cut = CheckClosed(*this, FString::Printf(TEXT("Shape %d convex only"), Seed), MeshOf(Mesh(Convex, Voxels)));
		TestTrue(*FString::Printf(TEXT("Shape %d: convex bevels only remove (%.3f < %d)"), Seed, Cut, Cells.Num()), Cut < Cells.Num());

		UPCGVoxelToDynMeshSettings* Concave = MakeBevel(0.0f);
		Concave->BevelControls = EPCGVoxelBevelControls::ConvexConcave;
		Concave->ConvexBevel = 0.0f;
		Concave->ConcaveBevel = 0.6f;
		const double Filled = CheckClosed(*this, FString::Printf(TEXT("Shape %d concave only"), Seed), MeshOf(Mesh(Concave, Voxels)));
		TestTrue(*FString::Printf(TEXT("Shape %d: concave bevels only add (%.3f > %d)"), Seed, Filled, Cells.Num()), Filled > Cells.Num());
	}

	// A solid block chamfered all round: exact, since no face has two bevels that meet.
	const double BlockVolume = CheckClosed(*this, TEXT("4x3x2 block at 0.5"), MeshOf(Mesh(MakeBevel(0.5f), MakeVoxels(Block(4, 3, 2)))));
	// Box minus 12 edge prisms (a^2/2 times the edge length minus 2a) minus 8 corners of 5a^3/6.
	constexpr double A = 0.5;
	const double Expected = 24.0 - A * A / 2.0 * (4.0 * (4.0 - 2.0 * A) + 4.0 * (3.0 - 2.0 * A) + 4.0 * (2.0 - 2.0 * A)) - 8.0 * 5.0 / 6.0 * A * A * A;
	CheckVolume(*this, TEXT("4x3x2 block at 0.5"), BlockVolume, Expected);
	return true;
}

#endif

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBevelEveryFillingTest,
	"PCGUtils.Voxel.Bevel.EveryFillingOfA3x2x2BlockIsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBevelEveryFillingTest::RunTest(const FString&)
{
	using namespace PCGVoxelBevelTests;
	// All 4095 fillings of a 3x2x2 block (every kind of notch, pocket, overhang and edge or corner contact that fits),
	// each under ten bevel setups: convex only, concave only and both, at 0.4, 0.5 (bevels meeting exactly) and 1, plus
	// a mixed Full setup. Every mesh must be closed and valid.
	int32 Failures = 0;
	for (int32 Setup = 0; Setup < 10; ++Setup)
	{
		for (int32 Mask = 1; Mask < 4096; ++Mask)
		{
			TArray<FIntVector> Cells;
			for (int32 Bit = 0; Bit < 12; ++Bit)
			{
				if (Mask & (1 << Bit))
				{
					Cells.Add(FIntVector(Bit % 3, (Bit / 3) % 2, Bit / 6));
				}
			}
			UPCGVoxelToDynMeshSettings* Settings = MakeBevel(0.0f);
			if (Setup < 9)
			{
				constexpr float Amounts[3] = { 0.4f, 0.5f, 1.0f };
				const int32 Kinds = Setup / 3;
				const float Amount = Amounts[Setup % 3];
				Settings->BevelControls = EPCGVoxelBevelControls::ConvexConcave;
				Settings->ConvexBevel = Kinds == 1 ? 0.0f : Amount;
				Settings->ConcaveBevel = Kinds == 0 ? 0.0f : Amount;
			}
			else
			{
				Settings->BevelControls = EPCGVoxelBevelControls::Full;
				Settings->ConvexTopBevel = 0.2f;
				Settings->ConvexSideBevel = 0.5f;
				Settings->ConvexBottomBevel = 0.0f;
				Settings->ConcaveTopBevel = 0.5f;
				Settings->ConcaveSideBevel = 0.0f;
				Settings->ConcaveBottomBevel = 0.3f;
			}
			const UE::Geometry::FDynamicMesh3* Result = MeshOf(Mesh(Settings, MakeVoxels(Cells)));
			const bool bClosed = Result && OpenEdges(*Result) == 0
				&& Result->CheckValidity(UE::Geometry::FDynamicMesh3::FValidityOptions(), UE::Geometry::EValidityCheckFailMode::ReturnOnly);
			if (!bClosed && ++Failures <= 5)
			{
				AddError(FString::Printf(TEXT("Setup %d, cells mask %d: the beveled mesh is not closed and valid (%d open edges)"),
					Setup, Mask, Result ? OpenEdges(*Result) : -1));
			}
		}
	}
	TestEqual(TEXT("Every filling under every setup is closed"), Failures, 0);
	return true;
}
#endif
