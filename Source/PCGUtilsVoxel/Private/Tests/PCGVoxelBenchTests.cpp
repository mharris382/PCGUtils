// Copyright Max Harris
//
// Voxel | To DynMesh timings. Not part of the PCGUtils suite (the name starts PCGUtilsPerf) because it takes seconds
// and checks speed, not behaviour. Run with: Automation RunTests PCGUtilsPerf.Voxel

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelAirFlow.h"
#include "Elements/PCGVoxelBoolean.h"
#include "Elements/PCGVoxelRasterize.h"
#include "Generators/SphereGenerator.h"
#include "Elements/PCGVoxelCompress.h"
#include "Elements/PCGVoxelShiftSeams.h"
#include "Elements/PCGVoxelToDynMesh.h"
#include "Math/RandomStream.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelMesher.h"

namespace PCGVoxelBenchTests
{
	using namespace PCGVoxelTestHelpers;

	struct FShape
	{
		FString Name;
		TArray<FIntVector> Cells;
	};

	TArray<FShape> Shapes()
	{
		TArray<FShape> Out;
		Out.Add({ TEXT("Solid 64^3"), Block(64, 64, 64) });

		// Rolling terrain 256 x 256, 4 to 36 cells high.
		FShape& Terrain = Out.AddDefaulted_GetRef();
		Terrain.Name = TEXT("Terrain 256x256");
		for (int32 Y = 0; Y < 256; ++Y)
		{
			for (int32 X = 0; X < 256; ++X)
			{
				const double H = 20.0 + 10.0 * FMath::Sin(X * 0.07) * FMath::Cos(Y * 0.05) + 6.0 * FMath::Sin((X + Y) * 0.21);
				for (int32 Z = 0; Z < FMath::RoundToInt(H); ++Z)
				{
					Terrain.Cells.Add(FIntVector(X, Y, Z));
				}
			}
		}

		// Half the cells of a 48^3 block at random: nearly every cell is exposed, the worst case for faces.
		FShape& Noise = Out.AddDefaulted_GetRef();
		Noise.Name = TEXT("Random 50% 48^3");
		FRandomStream Random(7);
		for (const FIntVector& Cell : Block(48, 48, 48))
		{
			if (Random.FRand() < 0.5f)
			{
				Noise.Cells.Add(Cell);
			}
		}
		return Out;
	}

	FPCGVoxelGrid GridOf(const TArray<FIntVector>& Cells)
	{
		FPCGVoxelGrid Grid;
		Grid.PointFirst = Cells;
		Grid.BuildCells();
		return Grid;
	}

	FString StageText(const FPCGVoxelMeshStats& Stats)
	{
		FString Text;
		for (int32 Stage = 0; Stage < FPCGVoxelMeshStats::NumStages; ++Stage)
		{
			Text += FString::Printf(TEXT(" %s %.1f"), FPCGVoxelMeshStats::StageName(Stage), Stats.StageSeconds[Stage] * 1000.0);
		}
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMesherBenchTest,
	"PCGUtilsPerf.Voxel.ToDynMesh.Timings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

bool FPCGVoxelMesherBenchTest::RunTest(const FString&)
{
	using namespace PCGVoxelBenchTests;

	struct FCase
	{
		const TCHAR* Name;
		bool bCollapse;
		bool bColors;
	};
	const FCase Cases[] = {
		{ TEXT("plain"), false, false },
		{ TEXT("collapse"), true, false },
		{ TEXT("smooth color + collapse"), true, true },
	};

	for (const FShape& Shape : Shapes())
	{
		double Start = FPlatformTime::Seconds();
		const FPCGVoxelGrid Grid = GridOf(Shape.Cells);
		const double GridMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		AddInfo(FString::Printf(TEXT("[%s] %d cells, grid lookup built in %.1f ms"), *Shape.Name, Grid.NumCells(), GridMs));

		TArray<FVector4f> Colors;
		Colors.SetNumUninitialized(Grid.NumPoints());
		for (int32 Point = 0; Point < Colors.Num(); ++Point)
		{
			// Bands of 4 cells, so collapse still finds equal neighbours.
			const int32 Band = Grid.PointFirst[Point].Z / 4;
			Colors[Point] = FVector4f((Band % 3) / 2.0f, (Band % 5) / 4.0f, 0.5f, 1.0f);
		}

		for (const FCase& Case : Cases)
		{
			FPCGVoxelMeshInput Input;
			Input.Grid = &Grid;
			if (Case.bColors)
			{
				Input.Color = Colors;
			}
			FPCGVoxelMeshOptions Options;
			Options.bCollapseCoplanarFaces = Case.bCollapse;
			Options.ColorTransition = Case.bColors ? EPCGVoxelColorTransition::Smooth : EPCGVoxelColorTransition::Hard;

			// Best of three, so a one-off hitch does not count.
			double Best = TNumericLimits<double>::Max();
			FPCGVoxelMeshStats BestStats;
			int32 Triangles = 0;
			for (int32 Run = 0; Run < 3; ++Run)
			{
				UE::Geometry::FDynamicMesh3 Mesh;
				FPCGVoxelMeshStats Stats;
				Start = FPlatformTime::Seconds();
				PCGVoxelMesher::BuildMesh(Input, Options, Mesh, &Stats);
				const double Seconds = FPlatformTime::Seconds() - Start;
				if (Seconds < Best)
				{
					Best = Seconds;
					BestStats = Stats;
					Triangles = Mesh.TriangleCount();
				}
			}
			AddInfo(FString::Printf(TEXT("[%s] %s: %.1f ms, %d faces, %d triangles |%s"),
				*Shape.Name, Case.Name, Best * 1000.0, BestStats.NumFaces, Triangles, *StageText(BestStats)));
		}

		// Bevel, every edge at half a cell. Also counts open edges: a large real shape is the best closedness check.
		{
			FPCGVoxelMeshInput Input;
			Input.Grid = &Grid;
			FPCGVoxelMeshOptions Options;
			Options.bBevel = true;
			for (int32 Kind = 0; Kind < 2; ++Kind)
			{
				for (int32 Facing = 0; Facing < 3; ++Facing)
				{
					Options.BevelAmount[Kind][Facing] = 0.5f;
				}
			}
			UE::Geometry::FDynamicMesh3 Mesh;
			FPCGVoxelMeshStats Stats;
			Start = FPlatformTime::Seconds();
			PCGVoxelMesher::BuildMesh(Input, Options, Mesh, &Stats);
			const double Seconds = FPlatformTime::Seconds() - Start;
			int32 Open = 0;
			for (const int32 EdgeID : Mesh.BoundaryEdgeIndicesItr())
			{
				(void)EdgeID;
				++Open;
			}
			AddInfo(FString::Printf(TEXT("[%s] bevel 0.5: %.1f ms, %d polygons, %d triangles, %d open edges, %d rejected, %d open fans |%s"),
				*Shape.Name, Seconds * 1000.0, Stats.NumRegions, Mesh.TriangleCount(), Open, Stats.NumRejectedTriangles, Stats.NumOpenFans, *StageText(Stats)));
		}

		// The engine floor: appending the plain mesh's vertices and triangles to a fresh mesh, bare and with the
		// attribute set enabled, as the mesher's emit stage does.
		{
			UE::Geometry::FDynamicMesh3 Source;
			FPCGVoxelMeshInput Input;
			Input.Grid = &Grid;
			PCGVoxelMesher::BuildMesh(Input, FPCGVoxelMeshOptions(), Source);
			for (const bool bAttributes : { false, true })
			{
				UE::Geometry::FDynamicMesh3 Copy;
				Start = FPlatformTime::Seconds();
				if (bAttributes)
				{
					Copy.EnableTriangleGroups();
					Copy.EnableAttributes();
					Copy.Attributes()->EnableMaterialID();
				}
				for (const int32 VertexID : Source.VertexIndicesItr())
				{
					Copy.AppendVertex(Source.GetVertex(VertexID));
				}
				for (const int32 TriangleID : Source.TriangleIndicesItr())
				{
					Copy.AppendTriangle(Source.GetTriangle(TriangleID));
				}
				AddInfo(FString::Printf(TEXT("[%s] engine floor, %s: %.1f ms for %d triangles"), *Shape.Name,
					bAttributes ? TEXT("with attributes") : TEXT("bare"), (FPlatformTime::Seconds() - Start) * 1000.0, Copy.TriangleCount()));
			}
		}

		// The whole node: reading point data, checking the contract, meshing, wrapping the result.
		const UPCGPointArrayData* Points = MakeVoxels(Shape.Cells);
		UPCGVoxelToDynMeshSettings* Settings = NewObject<UPCGVoxelToDynMeshSettings>();
		Settings->bConvertToLocalSpace = false;
		{
			FPCGContext Context;
			FPCGVoxelGrid FromPoints;
			Start = FPlatformTime::Seconds();
			const bool bBuilt = FPCGVoxelGrid::Build(&Context, Points, Settings->Grid, FText::FromString(TEXT("Bench")), 0, FromPoints);
			AddInfo(FString::Printf(TEXT("[%s] grid read from point data: %.1f ms (%s)"), *Shape.Name,
				(FPlatformTime::Seconds() - Start) * 1000.0, FromPoints.IsDense() ? TEXT("dense") : TEXT("hash map")));
			TestTrue(*FString::Printf(TEXT("[%s] grid read"), *Shape.Name), bBuilt);
		}
		Start = FPlatformTime::Seconds();
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelToDynMeshElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Points } });
		const double NodeMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		TestNotNull(*FString::Printf(TEXT("[%s] node made a mesh"), *Shape.Name), MeshOf(Outputs));
		AddInfo(FString::Printf(TEXT("[%s] whole node (defaults): %.1f ms"), *Shape.Name, NodeMs));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelShiftSeamsBenchTest,
	"PCGUtilsPerf.Voxel.ShiftSeams.Timings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelShiftSeamsBenchTest::RunTest(const FString&)
{
	using namespace PCGVoxelBenchTests;

	struct FCase { FString Name; TArray<FIntVector> Cells; FIntVector MaxSpan; };
	TArray<FCase> Cases;
	Cases.Add({ TEXT("Solid 32^3, spans 3"), Block(32, 32, 32), FIntVector(3) });
	Cases.Add({ TEXT("Wall 64x1x64, spans 3x1x3"), Block(64, 1, 64), FIntVector(3, 1, 3) });
	Cases.Add({ TEXT("Bricks 64x1x64, spans 4x1x1"), Block(64, 1, 64), FIntVector(4, 1, 1) });
	TArray<FIntVector> Terrain;
	for (const FShape& Shape : Shapes())
	{
		if (Shape.Name.StartsWith(TEXT("Terrain")))
		{
			Terrain = Shape.Cells;
		}
	}
	Cases.Add({ TEXT("Terrain 256x256, spans 3"), Terrain, FIntVector(3) });

	for (const FCase& Case : Cases)
	{
		UPCGVoxelCompressSettings* Compress = NewObject<UPCGVoxelCompressSettings>();
		Compress->Shape = EPCGVoxelChunkShape::Random;
		Compress->RandomMaxSpan = Case.MaxSpan;
		UPCGPointArrayData* Voxels = MakeVoxels(Case.Cells);
		Voxels->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements)->DeleteAttribute(TEXT("Id")); // unique per cell: nothing would merge
		const double T0 = FPlatformTime::Seconds();
		const TArray<FPCGTaggedData> Tiled = Run<FPCGVoxelCompressElement>(Compress, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		const double T1 = FPlatformTime::Seconds();
		const UPCGBasePointData* In = Cast<UPCGBasePointData>(Tiled[0].Data);

		UPCGVoxelShiftSeamsSettings* Shift = NewObject<UPCGVoxelShiftSeamsSettings>();
		const TArray<FPCGTaggedData> Shifted = Run<FPCGVoxelShiftSeamsElement>(Shift, { { PCGPinConstants::DefaultInputLabel, In } });
		const double T2 = FPlatformTime::Seconds();
		const UPCGBasePointData* Out = Cast<UPCGBasePointData>(Shifted[0].Data);

		int32 Moved = 0;
		for (int32 I = 0; I < Out->GetNumPoints(); ++I)
		{
			Moved += (Out->GetConstBoundsMinValueRange()[I].Equals(In->GetConstBoundsMinValueRange()[I])
				&& Out->GetConstBoundsMaxValueRange()[I].Equals(In->GetConstBoundsMaxValueRange()[I])) ? 0 : 1;
		}
		AddInfo(FString::Printf(TEXT("[%s] %d cells -> %d boxes: Compress Random %.1f ms, Shift Seams %.1f ms, %d boxes changed (%.0f%%)"),
			*Case.Name, Case.Cells.Num(), Out->GetNumPoints(), (T1 - T0) * 1000.0, (T2 - T1) * 1000.0, Moved,
			100.0 * Moved / FMath::Max(1, Out->GetNumPoints())));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelAirFlowBenchTest,
	"PCGUtilsPerf.Voxel.AirFlow.Timings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelAirFlowBenchTest::RunTest(const FString&)
{
	using namespace PCGVoxelBenchTests;
	for (const FShape& Shape : Shapes())
	{
		const UPCGPointArrayData* Points = MakeVoxels(Shape.Cells);
		for (const int32 AirCellVoxels : { 4, 2, 1 })
		{
			UPCGVoxelAirFlowSettings* Settings = NewObject<UPCGVoxelAirFlowSettings>();
			Settings->AirCellVoxels = AirCellVoxels;
			Settings->bGroundAtBottom = true;
			Settings->TurbulenceStrength = 0.3;
			const double Start = FPlatformTime::Seconds();
			const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelAirFlowElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Points } });
			const double Seconds = FPlatformTime::Seconds() - Start;
			const UPCGBasePointData* Flow = Outputs.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Outputs[0].Data);
			AddInfo(FString::Printf(TEXT("%s (%d cells), air cell %d voxels: %d air points in %.3f s"),
				*Shape.Name, Shape.Cells.Num(), AirCellVoxels, Flow ? Flow->GetNumPoints() : -1, Seconds));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizeBenchTest,
	"PCGUtilsPerf.Voxel.Rasterize.Timings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizeBenchTest::RunTest(const FString&)
{
	using namespace PCGVoxelBenchTests;
	// A sphere 50 cells across, finely tessellated, rasterized in every mode, then cut out of a block round it.
	UE::Geometry::FSphereGenerator Generator;
	Generator.Radius = 2500.0;
	Generator.NumPhi = 256;
	Generator.NumTheta = 512;
	Generator.Generate();
	UPCGDynamicMeshData* Mesh = NewObject<UPCGDynamicMeshData>();
	Mesh->Initialize(UE::Geometry::FDynamicMesh3(&Generator));
	const int32 Triangles = Mesh->GetDynamicMesh()->GetMeshRef().TriangleCount();

	UPCGVoxelRasterizeSettings* Settings = NewObject<UPCGVoxelRasterizeSettings>();
	Settings->bMeshInLocalSpace = false;
	Settings->Modes = static_cast<uint8>(EPCGVoxelRasterizeModes::Fill | EPCGVoxelRasterizeModes::Intersect | EPCGVoxelRasterizeModes::Surround);
	double Start = FPlatformTime::Seconds();
	const TArray<FPCGTaggedData> Raster = Run<FPCGVoxelRasterizeElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Mesh } });
	const double RasterSeconds = FPlatformTime::Seconds() - Start;
	const UPCGBasePointData* Cells = Raster.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Raster[0].Data);
	AddInfo(FString::Printf(TEXT("Rasterize sphere (%d triangles), all modes: %d cells in %.3f s"), Triangles, Cells ? Cells->GetNumPoints() : -1, RasterSeconds));

	TArray<FIntVector> BlockCells;
	for (int32 Z = -30; Z < 30; ++Z)
		for (int32 Y = -30; Y < 30; ++Y)
			for (int32 X = -30; X < 30; ++X)
				BlockCells.Add(FIntVector(X, Y, Z));
	const UPCGPointArrayData* Block = MakeVoxels(BlockCells);
	for (const EPCGVoxelBooleanOperation Operation : { EPCGVoxelBooleanOperation::Subtract, EPCGVoxelBooleanOperation::Union })
	{
		UPCGVoxelBooleanSettings* BooleanSettings = NewObject<UPCGVoxelBooleanSettings>();
		BooleanSettings->Operation = Operation;
		Start = FPlatformTime::Seconds();
		const TArray<FPCGTaggedData> Out = Run<FPCGVoxelBooleanElement>(BooleanSettings,
			{ { PCGPinConstants::DefaultInputLabel, Block }, { PCGVoxelBooleanPins::Operand, Cells } });
		const UPCGBasePointData* Result = Out.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Out[0].Data);
		AddInfo(FString::Printf(TEXT("Boolean %s, 216000-cell block and the sphere: %d cells in %.3f s"),
			Operation == EPCGVoxelBooleanOperation::Subtract ? TEXT("Subtract") : TEXT("Union"), Result ? Result->GetNumPoints() : -1,
			FPlatformTime::Seconds() - Start));
	}
	return true;
}

#endif
