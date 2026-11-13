// Copyright Max Harris
//
// Voxel | Rasterize (mesh and path to cells: Fill, Intersect, Surround) and Voxel | Boolean (union, intersect,
// subtract by cell index).

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelBoolean.h"
#include "Elements/PCGVoxelRasterize.h"
#include "Generators/SphereGenerator.h"
#include "PCGVoxelTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelRasterizeTests
{
	using namespace PCGVoxelTestHelpers;

	const UPCGBasePointData* PointsOf(const TArray<FPCGTaggedData>& Outputs, int32 Index = 0)
	{
		return Outputs.IsValidIndex(Index) ? Cast<UPCGBasePointData>(Outputs[Index].Data) : nullptr;
	}

	/** The cells of a voxel set and the point filling each; empty when it does not read as a voxel set. */
	TMap<FIntVector, int32> CellsOf(const UPCGBasePointData* Points, FPCGVoxelGrid* OutGrid = nullptr)
	{
		TMap<FIntVector, int32> Cells;
		FPCGVoxelGrid Grid;
		FPCGContext Context;
		if (Points && FPCGVoxelGrid::Build(&Context, Points, FPCGVoxelGridReadSettings(), FText::GetEmpty(), 0, Grid))
		{
			for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
			{
				Cells.Add(Grid.Cells[Index], Grid.CellPoint[Index]);
			}
		}
		if (OutGrid)
		{
			*OutGrid = Grid;
		}
		return Cells;
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Default)
	{
		const FPCGMetadataDomain* Domain = Points && Index >= 0 ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Default;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}

	/** Mode bits of the point on Cell, or -1 when that cell is not output. */
	int32 ModeAt(const UPCGBasePointData* Points, const TMap<FIntVector, int32>& Cells, const FIntVector& Cell)
	{
		const int32* Point = Cells.Find(Cell);
		return Point ? ValueOf<int32>(Points, TEXT("RasterMode"), *Point, -2) : -1;
	}

	UPCGDynamicMeshData* MeshData(UE::Geometry::FDynamicMesh3&& Mesh)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	UPCGDynamicMeshData* BoxMesh(const FVector& Center, const FVector& Extents)
	{
		UE::Geometry::FMinimalBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(Center, Extents);
		Generator.Generate();
		return MeshData(UE::Geometry::FDynamicMesh3(&Generator));
	}

	UPCGPointArrayData* PathPoints(const TArray<FVector>& Positions)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Positions.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::Transform);
		TPCGValueRange<FTransform> Transforms = Points->GetTransformValueRange();
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Positions[Index]);
		}
		return Points;
	}

	UPCGVoxelRasterizeSettings* Rasterizer(uint8 Modes)
	{
		UPCGVoxelRasterizeSettings* Settings = NewObject<UPCGVoxelRasterizeSettings>();
		Settings->bMeshInLocalSpace = false;
		Settings->Modes = Modes;
		return Settings;
	}

	constexpr uint8 Fill = static_cast<uint8>(EPCGVoxelRasterizeModes::Fill);
	constexpr uint8 Intersect = static_cast<uint8>(EPCGVoxelRasterizeModes::Intersect);
	constexpr uint8 Surround = static_cast<uint8>(EPCGVoxelRasterizeModes::Surround);

	TArray<FPCGTaggedData> Rasterize(UPCGVoxelRasterizeSettings* Settings, const UPCGData* Shape, const UPCGData* GridSet = nullptr)
	{
		TArray<TPair<FName, const UPCGData*>> Inputs = { { PCGPinConstants::DefaultInputLabel, Shape } };
		if (GridSet)
		{
			Inputs.Add({ PCGVoxelRasterizePins::Grid, GridSet });
		}
		return Run<FPCGVoxelRasterizeElement>(Settings, Inputs);
	}

	TArray<FPCGTaggedData> Boolean(EPCGVoxelBooleanOperation Operation, const UPCGData* In, const UPCGData* Operand,
		EPCGVoxelBooleanAddedValues Added = EPCGVoxelBooleanAddedValues::OperandThenNearest)
	{
		UPCGVoxelBooleanSettings* Settings = NewObject<UPCGVoxelBooleanSettings>();
		Settings->Operation = Operation;
		Settings->AddedValues = Added;
		return Run<FPCGVoxelBooleanElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In }, { PCGVoxelBooleanPins::Operand, Operand } });
	}

	/** Moves a set's grid (and its points with it) by Offset, writing the grid transform. */
	void ShiftGrid(UPCGBasePointData* Points, const FVector& Offset)
	{
		TPCGValueRange<FTransform> Transforms = Points->GetTransformValueRange();
		for (FTransform& Transform : Transforms)
		{
			Transform.AddToTranslation(Offset);
		}
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Points), TEXT("VoxelGridTransform"), FTransform(Offset));
	}

	void AddDoubleAttribute(UPCGBasePointData* Points, FName Name, const TArray<double>& Values)
	{
		FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<double>* Attribute = Domain->CreateAttribute<double>(Name, 0.0, false);
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			Attribute->SetValue(Entries[Index], Values[Index]);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizeMeshBoxTest,
	"PCGUtils.Voxel.Rasterize.MeshBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizeMeshBoxTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	// A bar inside cells x 0..2: centres inside and faces inside the same cells.
	const UPCGData* Bar = BoxMesh(FVector(100.0, 0.0, 0.0), FVector(140.0, 40.0, 40.0));
	{
		const TArray<FPCGTaggedData> Out = Rasterize(Rasterizer(Fill), Bar);
		const UPCGBasePointData* Points = PointsOf(Out);
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Fill: three cells"), Cells.Num(), 3);
		TestTrue(TEXT("Fill: cells 0..2 along X"), Cells.Contains(FIntVector(0, 0, 0)) && Cells.Contains(FIntVector(2, 0, 0)));
		TestEqual(TEXT("The bar's cells are both inside and touched (3)"), ModeAt(Points, Cells, FIntVector(1, 0, 0)), 3);
		TestTrue(TEXT("Output carries the input's tags"), Out.Num() == 1 && Out[0].Tags.Contains(TEXT("KeptTag")));
	}
	TestEqual(TEXT("Intersect: the same three cells"), CellsOf(PointsOf(Rasterize(Rasterizer(Intersect), Bar))).Num(), 3);
	{
		const UPCGBasePointData* Points = PointsOf(Rasterize(Rasterizer(Surround), Bar));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Surround (all neighbours): a 5x3x3 shell less the bar"), Cells.Num(), 42);
		TestFalse(TEXT("Surround leaves the bar out"), Cells.Contains(FIntVector(1, 0, 0)));
		TestEqual(TEXT("Surround cells carry bit 4 only"), ModeAt(Points, Cells, FIntVector(-1, 0, 0)), 4);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Surround);
		Settings->SurroundNeighbors = EPCGVoxelRasterizeNeighbors::Faces;
		TestEqual(TEXT("Surround (faces): 2 ends + 4 sides x 3"), CellsOf(PointsOf(Rasterize(Settings, Bar))).Num(), 14);
		Settings->SurroundCells = 2;
		TestTrue(TEXT("Two cells of shell reach further"), CellsOf(PointsOf(Rasterize(Settings, Bar))).Contains(FIntVector(-2, 0, 0)));
	}
	TestEqual(TEXT("Fill + Intersect + Surround: the whole 5x3x3 box"),
		CellsOf(PointsOf(Rasterize(Rasterizer(Fill | Intersect | Surround), Bar))).Num(), 45);

	// A box whose faces lie exactly on cell faces: it fills its 3 cells and touches no other layer.
	const UPCGData* Aligned = BoxMesh(FVector(100.0, 0.0, 0.0), FVector(150.0, 50.0, 50.0));
	{
		const UPCGBasePointData* Points = PointsOf(Rasterize(Rasterizer(Fill | Intersect), Aligned));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Faces on the grid: exactly the three cells"), Cells.Num(), 3);
		TestEqual(TEXT("Faces on the grid touch no cell (mode 1)"), ModeAt(Points, Cells, FIntVector(0, 0, 0)), 1);
		TestEqual(TEXT("Surround still wraps the filled cells"), CellsOf(PointsOf(Rasterize(Rasterizer(Surround), Aligned))).Num(), 42);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Intersect);
		Settings->TouchMargin = 0.2;
		TestEqual(TEXT("Touch Margin 0.2 counts both sides of an on-grid face"), CellsOf(PointsOf(Rasterize(Settings, Aligned))).Num(), 45);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizeMeshSphereTest,
	"PCGUtils.Voxel.Rasterize.MeshSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizeMeshSphereTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	UE::Geometry::FSphereGenerator Generator;
	Generator.Radius = 340.0;
	Generator.NumPhi = 32;
	Generator.NumTheta = 64;
	Generator.Generate();
	UE::Geometry::FDynamicMesh3 Sphere(&Generator);
	const UPCGData* Mesh = MeshData(MoveTemp(Sphere));

	const UPCGBasePointData* Points = PointsOf(Rasterize(Rasterizer(Fill | Intersect | Surround), Mesh));
	const TMap<FIntVector, int32> Cells = CellsOf(Points);
	TestTrue(TEXT("The output reads as a voxel set"), Cells.Num() > 0);

	int32 Inside = 0, Expected = 0, Bad = 0, Open = 0;
	for (int32 Z = -5; Z <= 5; ++Z)
		for (int32 Y = -5; Y <= 5; ++Y)
			for (int32 X = -5; X <= 5; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				const double R = FVector(Cell).Size();
				Expected += R < 3.35 ? 1 : 0; // a touch under 3.4: the faceted sphere is a little smaller
				const int32 Mode = ModeAt(Points, Cells, Cell);
				if (Mode >= 0 && (Mode & 1))
				{
					++Inside;
					Bad += R > 3.4 ? 1 : 0;
					// Every neighbour of an inside cell is output: the shell is closed.
					for (int32 DZ = -1; DZ <= 1; ++DZ)
						for (int32 DY = -1; DY <= 1; ++DY)
							for (int32 DX = -1; DX <= 1; ++DX)
							{
								Open += Cells.Contains(Cell + FIntVector(DX, DY, DZ)) ? 0 : 1;
							}
				}
				if (Mode == 4 && R < 2.4)
				{
					++Bad; // a surround cell deep inside
				}
				if (Mode >= 0 && (Mode & 2) && FMath::Abs(R - 3.4) > 0.5 * FMath::Sqrt(3.0) + 0.01)
				{
					++Bad; // a touched cell the surface cannot reach
				}
			}
	TestTrue(*FString::Printf(TEXT("Fill is close to the lattice count (%d against %d)"), Inside, Expected), FMath::Abs(Inside - Expected) <= 8);
	TestEqual(TEXT("No cell is in a mode it cannot be in"), Bad, 0);
	TestEqual(TEXT("Fill, Intersect and Surround together leave no gap round the inside"), Open, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizePathTest,
	"PCGUtils.Voxel.Rasterize.Path",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizePathTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	const UPCGData* Square = PathPoints({ FVector(-140, -140, 0), FVector(240, -140, 0), FVector(240, 240, 0), FVector(-140, 240, 0) });
	auto PathSettings = [](uint8 Modes)
	{
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Modes);
		Settings->Source = EPCGVoxelRasterizeSource::Path;
		return Settings;
	};
	{
		const UPCGBasePointData* Points = PointsOf(Rasterize(PathSettings(Fill), Square));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Fill: 4x4 cells"), Cells.Num(), 16);
		TestEqual(TEXT("The ring is inside and touched"), ModeAt(Points, Cells, FIntVector(-1, -1, 0)), 3);
		TestEqual(TEXT("The middle is inside only"), ModeAt(Points, Cells, FIntVector(0, 0, 0)), 1);
	}
	TestEqual(TEXT("Intersect: the ring of 12"), CellsOf(PointsOf(Rasterize(PathSettings(Intersect), Square))).Num(), 12);
	TestEqual(TEXT("Surround (8 neighbours): 6x6 less 4x4"), CellsOf(PointsOf(Rasterize(PathSettings(Surround), Square))).Num(), 20);
	{
		UPCGVoxelRasterizeSettings* Settings = PathSettings(Surround);
		Settings->SurroundNeighbors = EPCGVoxelRasterizeNeighbors::Faces;
		TestEqual(TEXT("Surround (4 neighbours): no corners"), CellsOf(PointsOf(Rasterize(Settings, Square))).Num(), 16);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathSettings(Fill);
		Settings->CellsAbove = 1;
		const TMap<FIntVector, int32> Cells = CellsOf(PointsOf(Rasterize(Settings, Square)));
		TestEqual(TEXT("Cells Above 1: two layers"), Cells.Num(), 32);
		TestTrue(TEXT("The second layer is above"), Cells.Contains(FIntVector(0, 0, 1)));
	}
	const UPCGData* Closed = PathPoints({ FVector(-140, -140, 0), FVector(240, -140, 0), FVector(240, 240, 0), FVector(-140, 240, 0), FVector(-140, -140, 0) });
	TestEqual(TEXT("A loop that repeats its first point gives the same cells"), CellsOf(PointsOf(Rasterize(PathSettings(Fill), Closed))).Num(), 16);
	return true;
}

namespace PCGVoxelRasterizeTests
{
	/** Runs Rasterize on paths, each with an optional tag (empty for none). */
	TArray<FPCGTaggedData> RasterizeTagged(UPCGVoxelRasterizeSettings* Settings, const TArray<TPair<const UPCGData*, FString>>& Shapes)
	{
		struct FExposed : FPCGVoxelRasterizeElement
		{
			using FPCGVoxelRasterizeElement::ExecuteInternal;
		};
		FPCGContext Context;
		Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
		for (const TPair<const UPCGData*, FString>& Shape : Shapes)
		{
			FPCGTaggedData& Tagged = Context.InputData.TaggedData.Emplace_GetRef();
			Tagged.Data = Shape.Key;
			Tagged.Pin = PCGPinConstants::DefaultInputLabel;
			if (!Shape.Value.IsEmpty())
			{
				Tagged.Tags.Add(Shape.Value);
			}
		}
		FExposed().ExecuteInternal(&Context);
		return Context.OutputData.TaggedData;
	}

	UPCGVoxelRasterizeSettings* PathRasterizer(uint8 Modes)
	{
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Modes);
		Settings->Source = EPCGVoxelRasterizeSource::Path;
		return Settings;
	}

	double BoundsMaxZAt(const UPCGBasePointData* Points, const TMap<FIntVector, int32>& Cells, const FIntVector& Cell)
	{
		const int32* Point = Cells.Find(Cell);
		return Point ? Points->GetConstBoundsMaxValueRange()[*Point].Z : -1000.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizePathHolesTest,
	"PCGUtils.Voxel.Rasterize.PathHoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizePathHolesTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	// Cells -2..4 (7 x 7) inside the shape, cells 0..2 (3 x 3) inside the hole; the hole's line runs through its outer ring.
	const UPCGData* Shape = PathPoints({ FVector(-240, -240, 0), FVector(440, -240, 0), FVector(440, 440, 0), FVector(-240, 440, 0) });
	const UPCGData* Hole = PathPoints({ FVector(-40, -40, 0), FVector(240, -40, 0), FVector(240, 240, 0), FVector(-40, 240, 0) });
	const UPCGData* FarHole = PathPoints({ FVector(5000, 5000, 0), FVector(5300, 5000, 0), FVector(5300, 5300, 0) });

	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		const TArray<FPCGTaggedData> Outputs = RasterizeTagged(Settings, { { Shape, FString() }, { Hole, TEXT("Hole") } });
		TestEqual(TEXT("Hole paths off: two outputs"), Outputs.Num(), 2);
		TestEqual(TEXT("Hole paths off: the hole-tagged path is a shape of its own"), CellsOf(PointsOf(Outputs, 1)).Num(), 9);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bUseHolePaths = true;
		const TArray<FPCGTaggedData> Outputs = RasterizeTagged(Settings, { { Shape, FString() }, { Hole, TEXT("Hole") }, { FarHole, TEXT("Hole") } });
		TestEqual(TEXT("Holes output nothing"), Outputs.Num(), 1);
		TestEqual(TEXT("Fill: 49 less the 9 in the hole"), CellsOf(PointsOf(Outputs)).Num(), 40);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill | Intersect);
		Settings->bUseHolePaths = true;
		const UPCGBasePointData* Points = PointsOf(RasterizeTagged(Settings, { { Shape, FString() }, { Hole, TEXT("Hole") } }));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Fill and Intersect: the hole's line ring stays as surface"), Cells.Num(), 48);
		TestEqual(TEXT("The hole's middle is gone"), ModeAt(Points, Cells, FIntVector(1, 1, 0)), -1);
		TestEqual(TEXT("The hole's line cells are surface, centre in the hole"), ModeAt(Points, Cells, FIntVector(0, 0, 0)), 2);
		TestEqual(TEXT("Inside the shape, off the hole"), ModeAt(Points, Cells, FIntVector(-1, -1, 0)), 1);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bUseHolePaths = true;
		Settings->HoleTag = TEXT("Cutout");
		const TArray<FPCGTaggedData> Outputs = RasterizeTagged(Settings, { { Shape, FString() }, { Hole, TEXT("Cutout") } });
		TestEqual(TEXT("A custom Hole Tag"), CellsOf(PointsOf(Outputs)).Num(), 40);
	}
	{
		// A hole's segments count for the heights: a raised hole pulls the cells next to it up.
		const UPCGData* RaisedHole = PathPoints({ FVector(-40, -40, 300), FVector(240, -40, 300), FVector(240, 240, 300), FVector(-40, 240, 300) });
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bUseHolePaths = true;
		Settings->PathHeight = EPCGVoxelRasterizePathHeight::Snap;
		const TMap<FIntVector, int32> Cells = CellsOf(PointsOf(RasterizeTagged(Settings, { { Shape, FString() }, { RaisedHole, TEXT("Hole") } })));
		TestTrue(TEXT("Next to the hole: at the hole's height"), Cells.Contains(FIntVector(-1, 1, 3)));
		TestTrue(TEXT("Next to the shape's edge: at its height"), Cells.Contains(FIntVector(-2, 1, 0)));
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bUseHolePaths = true;
		AddExpectedMessage(TEXT("there is no shape to cut them from"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		TestEqual(TEXT("Only holes: nothing"), RasterizeTagged(Settings, { { Hole, TEXT("Hole") } }).Num(), 0);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bUseHolePaths = true;
		Settings->HoleTag = NAME_None;
		AddExpectedError(TEXT("Hole Tag is empty while Use Hole Paths is on"), EAutomationExpectedErrorFlags::Contains, 1, false);
		RasterizeTagged(Settings, { { Shape, FString() } });
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizePathHeightTest,
	"PCGUtils.Voxel.Rasterize.PathHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizePathHeightTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	// 4 x 4 cells, flat at Z 130 (1.3 cells) and at Z 170 (1.7 cells).
	const UPCGData* Low = PathPoints({ FVector(-140, -140, 130), FVector(240, -140, 130), FVector(240, 240, 130), FVector(-140, 240, 130) });
	const UPCGData* High = PathPoints({ FVector(-140, -140, 170), FVector(240, -140, 170), FVector(240, 240, 170), FVector(-140, 240, 170) });
	auto HeightRun = [&](EPCGVoxelRasterizePathHeight Height, const UPCGData* Path, UPCGVoxelRasterizeSettings* Settings = nullptr)
	{
		if (!Settings)
		{
			Settings = PathRasterizer(Fill);
		}
		Settings->PathHeight = Height;
		return Rasterize(Settings, Path);
	};
	{
		const UPCGBasePointData* Points = PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, Low));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Snap: one layer"), Cells.Num(), 16);
		TestTrue(TEXT("Snap 1.3: layer 1"), Cells.Contains(FIntVector(0, 0, 1)));
		TestEqual(TEXT("Snap: full cell"), BoundsMaxZAt(Points, Cells, FIntVector(0, 0, 1)), 50.0, 1e-6);
		TestTrue(TEXT("Snap 1.7: layer 2"), CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, High))).Contains(FIntVector(0, 0, 2)));
	}
	{
		const UPCGBasePointData* Points = PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Trim, Low));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Trim: one layer"), Cells.Num(), 16);
		TestEqual(TEXT("Trim 1.3: the cell's top at 130"), BoundsMaxZAt(Points, Cells, FIntVector(0, 0, 1)), 30.0, 1e-6);
		const UPCGBasePointData* HighPoints = PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Trim, High));
		const TMap<FIntVector, int32> HighCells = CellsOf(HighPoints);
		TestEqual(TEXT("Trim 1.7: layer 2, top at 170"), BoundsMaxZAt(HighPoints, HighCells, FIntVector(0, 0, 2)), -30.0, 1e-6);
	}
	{
		const UPCGBasePointData* Points = PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::BestFit, Low));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Best Fit: two layers"), Cells.Num(), 32);
		TestEqual(TEXT("Best Fit: the top cell ends at 130"), BoundsMaxZAt(Points, Cells, FIntVector(0, 0, 1)), 30.0, 1e-6);
		TestEqual(TEXT("Best Fit: a full cell under it"), BoundsMaxZAt(Points, Cells, FIntVector(0, 0, 0)), 50.0, 1e-6);
	}

	// A one-cell strip (cells 0..4) rising 1 cell per cell: the path is at 40, 140, ... 440 over the cell centres.
	const UPCGData* Ramp = PathPoints({ FVector(-40, -20, 0), FVector(440, -20, 480), FVector(440, 20, 480), FVector(-40, 20, 0) });
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bWritePathZAttribute = true;
		const UPCGBasePointData* Points = PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, Ramp, Settings));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Ramp: one cell per column"), Cells.Num(), 5);
		for (int32 X = 0; X < 5; ++X)
		{
			TestTrue(FString::Printf(TEXT("Ramp: column %d at layer %d"), X, X), Cells.Contains(FIntVector(X, 0, X)));
			const int32* Point = Cells.Find(FIntVector(X, 0, X));
			TestEqual(FString::Printf(TEXT("Ramp: Path Z of column %d"), X),
				Point ? ValueOf<double>(Points, TEXT("PathZ"), *Point, -1.0) : -1.0, 40.0 + 100.0 * X, 1e-6);
		}
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bExtrudeDown = true;
		TestEqual(TEXT("Extrude Down: every column to the lowest layer"),
			CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, Ramp, Settings))).Num(), 1 + 2 + 3 + 4 + 5);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bExtrudeDown = true;
		Settings->ExtraCellsBelow.Constant = 1;
		const TMap<FIntVector, int32> Cells = CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, Ramp, Settings)));
		TestEqual(TEXT("Extrude Down, 1 extra cell"), Cells.Num(), 2 + 3 + 4 + 5 + 6);
		TestTrue(TEXT("The extra cell is under the lowest layer"), Cells.Contains(FIntVector(4, 0, -1)));
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->ExtraCellsBelow.Constant = 1;
		TestEqual(TEXT("1 extra cell under each surface cell"), CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, Ramp, Settings))).Num(), 10);
	}
	{
		UPCGPointArrayData* RampData = PathPoints({ FVector(-40, -20, 0), FVector(440, -20, 480), FVector(440, 20, 480), FVector(-40, 20, 0) });
		PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(RampData), TEXT("ExtraCellsBelow"), 2);
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->ExtraCellsBelow.SetAttribute(TEXT("@Data.ExtraCellsBelow"));
		TestEqual(TEXT("@Data Extra Cells Below 2"), CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Snap, RampData, Settings))).Num(), 15);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->ExtraCellsBelow.Constant = 2;
		TestEqual(TEXT("Flat: Extra Cells Below adds layers"), CellsOf(PointsOf(HeightRun(EPCGVoxelRasterizePathHeight::Flat, Low, Settings))).Num(), 16 * 3);
	}
	{
		UPCGVoxelRasterizeSettings* Settings = PathRasterizer(Fill);
		Settings->bWritePathZAttribute = true;
		Settings->PathZAttribute = TEXT("RasterMode");
		AddExpectedError(TEXT("Path Z Attribute and Mode Attribute are both 'RasterMode'"), EAutomationExpectedErrorFlags::Contains, 1, false);
		Rasterize(Settings, Low);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelRasterizeGridAndErrorsTest,
	"PCGUtils.Voxel.Rasterize.GridAndErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelRasterizeGridAndErrorsTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	// The Grid pin's frame is used: 50 cm cells, origin shifted 25 cm along X.
	UPCGPointArrayData* GridSet = MakeVoxels({ FIntVector(0, 0, 0) }, 50.0);
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(GridSet), TEXT("VoxelGridTransform"), FTransform(FVector(25.0, 0.0, 0.0)));
	{
		const UPCGBasePointData* Points = PointsOf(Rasterize(Rasterizer(Fill), BoxMesh(FVector(100.0, 0.0, 0.0), FVector(40.0)), GridSet));
		FPCGVoxelGrid Grid;
		const TMap<FIntVector, int32> Cells = CellsOf(Points, &Grid);
		TestEqual(TEXT("Grid pin: two 50 cm cells"), Cells.Num(), 2);
		TestTrue(TEXT("Grid pin: the Voxel Size is the grid's"), Grid.VoxelSize.Equals(FVector(50.0)));
		TestTrue(TEXT("Grid pin: the grid transform is the grid's"), Grid.GridToWorld.GetLocation().Equals(FVector(25.0, 0.0, 0.0)));
		TestTrue(TEXT("Grid pin: cells sit at 75 and 125"), Points && Points->GetNumPoints() == 2
			&& FMath::IsNearlyEqual(Points->GetConstTransformValueRange()[0].GetLocation().X, 75.0)
			&& FMath::IsNearlyEqual(Points->GetConstTransformValueRange()[1].GetLocation().X, 125.0));
	}

	AddExpectedError(TEXT("Modes has none of Fill, Intersect and Surround turned on"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Rasterize(Rasterizer(0), BoxMesh(FVector::ZeroVector, FVector(40.0)));

	AddExpectedError(TEXT("Source is Mesh, which reads DynMesh data"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Rasterize(Rasterizer(Fill), PathPoints({ FVector::ZeroVector, FVector(100, 0, 0), FVector(0, 100, 0) }));

	{
		AddExpectedError(TEXT("a closed loop needs at least 3"), EAutomationExpectedErrorFlags::Contains, 1, false);
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Fill);
		Settings->Source = EPCGVoxelRasterizeSource::Path;
		Rasterize(Settings, PathPoints({ FVector::ZeroVector, FVector(100, 0, 0) }));
	}
	{
		AddExpectedError(TEXT("over Max Cells (10)"), EAutomationExpectedErrorFlags::Contains, 1, false);
		UPCGVoxelRasterizeSettings* Settings = Rasterizer(Fill);
		Settings->MaxCells = 10;
		Rasterize(Settings, BoxMesh(FVector::ZeroVector, FVector(400.0)));
	}
	{
		// An open mesh (one quad): Intersect works without a word, Fill warns.
		UE::Geometry::FDynamicMesh3 Quad;
		const int32 A = Quad.AppendVertex(FVector3d(10, -150, -150));
		const int32 B = Quad.AppendVertex(FVector3d(10, 150, -150));
		const int32 C = Quad.AppendVertex(FVector3d(10, 150, 150));
		const int32 D = Quad.AppendVertex(FVector3d(10, -150, 150));
		Quad.AppendTriangle(A, B, C);
		Quad.AppendTriangle(A, C, D);
		const UPCGData* Open = MeshData(MoveTemp(Quad));
		TestEqual(TEXT("Intersect of a 300 cm quad at x = 10: a 1x3x3 slab"),
			CellsOf(PointsOf(Rasterize(Rasterizer(Intersect), Open))).Num(), 9);
		AddExpectedMessagePlain(TEXT("the mesh is not closed"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		Rasterize(Rasterizer(Fill), Open);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBooleanOperationsTest,
	"PCGUtils.Voxel.Boolean.Operations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBooleanOperationsTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	const UPCGData* Row = MakeVoxels(Block(4, 1, 1));
	const UPCGData* Middle = MakeVoxels({ FIntVector(1, 0, 0), FIntVector(2, 0, 0) });
	{
		const TArray<FPCGTaggedData> Out = Boolean(EPCGVoxelBooleanOperation::Subtract, Row, Middle);
		const UPCGBasePointData* Points = PointsOf(Out);
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Subtract: the two ends are left"), Cells.Num(), 2);
		TestTrue(TEXT("Subtract: ends keep their Ids"), Cells.Contains(FIntVector(0, 0, 0)) && Cells.Contains(FIntVector(3, 0, 0))
			&& ValueOf<int32>(Points, TEXT("Id"), Cells[FIntVector(3, 0, 0)], -1) == 3);
		TestTrue(TEXT("Subtract keeps tags"), Out.Num() == 1 && Out[0].Tags.Contains(TEXT("KeptTag")));
		const FPCGMetadataDomain* DataDomain = Points ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		TestTrue(TEXT("Subtract keeps @Data"), DataDomain && DataDomain->HasAttribute(TEXT("VoxelSize")));
	}
	{
		const TMap<FIntVector, int32> Cells = CellsOf(PointsOf(Boolean(EPCGVoxelBooleanOperation::Intersect, Row, Middle)));
		TestEqual(TEXT("Intersect: the shared middle"), Cells.Num(), 2);
		TestTrue(TEXT("Intersect: cells 1 and 2"), Cells.Contains(FIntVector(1, 0, 0)) && Cells.Contains(FIntVector(2, 0, 0)));
	}

	// Union values: In has Ids 0, 1 and OnlyIn 10, 20; Operand has OnlyOperand 7, 8.
	UPCGPointArrayData* Ends = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(5, 0, 0) });
	AddDoubleAttribute(Ends, TEXT("OnlyIn"), { 10.0, 20.0 });
	UPCGPointArrayData* Inner = MakeVoxels({ FIntVector(4, 0, 0), FIntVector(1, 0, 0) });
	AddDoubleAttribute(Inner, TEXT("OnlyOperand"), { 8.0, 7.0 });
	{
		const UPCGBasePointData* Points = PointsOf(Boolean(EPCGVoxelBooleanOperation::Union, Ends, Inner));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Union: four cells"), Cells.Num(), 4);
		const int32 Near0 = Cells.FindRef(FIntVector(1, 0, 0));
		const int32 Near5 = Cells.FindRef(FIntVector(4, 0, 0));
		TestEqual(TEXT("Union: the added cell takes the Operand's value"), ValueOf<double>(Points, TEXT("OnlyOperand"), Near0, -1.0), 7.0);
		TestEqual(TEXT("Union: ... and the Operand's Id"), ValueOf<int32>(Points, TEXT("Id"), Near5, -9), 0);
		TestEqual(TEXT("Union: an attribute only In has comes from the nearest In cell"), ValueOf<double>(Points, TEXT("OnlyIn"), Near5, -1.0), 20.0);
		TestEqual(TEXT("Union: In's own cells get the default of an Operand-only attribute"),
			ValueOf<double>(Points, TEXT("OnlyOperand"), Cells.FindRef(FIntVector(0, 0, 0)), -1.0), 0.0);
	}
	{
		const UPCGBasePointData* Points = PointsOf(Boolean(EPCGVoxelBooleanOperation::Union, Ends, Inner, EPCGVoxelBooleanAddedValues::Nearest));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("Nearest: cell 1 copies cell 0"), ValueOf<int32>(Points, TEXT("Id"), Cells.FindRef(FIntVector(1, 0, 0)), -9), 0);
		TestEqual(TEXT("Nearest: cell 4 copies cell 5"), ValueOf<int32>(Points, TEXT("Id"), Cells.FindRef(FIntVector(4, 0, 0)), -9), 1);
	}
	{
		const UPCGBasePointData* Points = PointsOf(Boolean(EPCGVoxelBooleanOperation::Union, Ends, Inner, EPCGVoxelBooleanAddedValues::None));
		const TMap<FIntVector, int32> Cells = CellsOf(Points);
		TestEqual(TEXT("None: added cells get the attribute default"), ValueOf<int32>(Points, TEXT("Id"), Cells.FindRef(FIntVector(1, 0, 0)), -9), -1);
		TestEqual(TEXT("None: In's cells keep their values"), ValueOf<int32>(Points, TEXT("Id"), Cells.FindRef(FIntVector(5, 0, 0)), -9), 1);
	}

	// Chunks: a 2x2x2 chunk stays whole when untouched and becomes cells when cut.
	const TArray<FIntVector> Spans = { FIntVector(2) };
	const UPCGData* Chunk = MakeVoxels({ FIntVector(0) }, 100.0, &Spans);
	{
		const UPCGBasePointData* Points = PointsOf(Boolean(EPCGVoxelBooleanOperation::Subtract, Chunk, MakeVoxels({ FIntVector(1, 1, 1) })));
		TestEqual(TEXT("Cut chunk: seven points"), Points ? Points->GetNumPoints() : -1, 7);
		TestEqual(TEXT("Cut chunk: seven cells, the cut one gone"), CellsOf(Points).Num(), 7);
	}
	{
		const UPCGBasePointData* Points = PointsOf(Boolean(EPCGVoxelBooleanOperation::Subtract, Chunk, MakeVoxels({ FIntVector(5, 5, 5) })));
		TestEqual(TEXT("Untouched chunk: still one point"), Points ? Points->GetNumPoints() : -1, 1);
		TestTrue(TEXT("Untouched chunk: span kept"), ValueOf<FVector>(Points, TEXT("VoxelSpan"), 0, FVector::ZeroVector).Equals(FVector(2.0)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBooleanGridsTest,
	"PCGUtils.Voxel.Boolean.GridsAndEmbedding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBooleanGridsTest::RunTest(const FString&)
{
	using namespace PCGVoxelRasterizeTests;

	const UPCGData* Row = MakeVoxels(Block(4, 1, 1));

	// An Operand on a grid shifted by a whole cell lines up.
	{
		UPCGPointArrayData* Shifted = MakeVoxels({ FIntVector(4, 0, 0) });
		ShiftGrid(Shifted, FVector(100.0, 0.0, 0.0));
		const TMap<FIntVector, int32> Cells = CellsOf(PointsOf(Boolean(EPCGVoxelBooleanOperation::Union, Row, Shifted)));
		TestTrue(TEXT("Whole-cell shift: Operand cell 4 lands on cell 5"), Cells.Num() == 5 && Cells.Contains(FIntVector(5, 0, 0)));
	}

	AddExpectedError(TEXT("Operand 0's Voxel Size, along this set's grid axes, is (50, 50, 50); this set's is (100, 100, 100)"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	Boolean(EPCGVoxelBooleanOperation::Subtract, Row, MakeVoxels({ FIntVector(0) }, 50.0));

	{
		AddExpectedError(TEXT("the two grids do not line up"), EAutomationExpectedErrorFlags::Contains, 1, false);
		UPCGPointArrayData* Half = MakeVoxels({ FIntVector(0) });
		ShiftGrid(Half, FVector(50.0, 0.0, 0.0));
		Boolean(EPCGVoxelBooleanOperation::Subtract, Row, Half);
	}

	// The designer's case: rasterize a mesh on a block's grid, then cut it out of the block.
	const UPCGData* BlockSet = MakeVoxels(Block(6, 4, 3));
	const UPCGBasePointData* Hole = PointsOf(Rasterize(Rasterizer(Fill | Intersect), BoxMesh(FVector(250.0, 150.0, 100.0), FVector(140.0, 90.0, 40.0)), BlockSet));
	TestEqual(TEXT("Embedding: the mesh covers 4 x 2 x 1 cells"), CellsOf(Hole).Num(), 8);
	const TMap<FIntVector, int32> Left = CellsOf(PointsOf(Boolean(EPCGVoxelBooleanOperation::Subtract, BlockSet, Hole)));
	TestEqual(TEXT("Embedding: 72 - 8 cells are left"), Left.Num(), 64);
	TestFalse(TEXT("Embedding: the mesh's cells are gone"), Left.Contains(FIntVector(1, 1, 1)) || Left.Contains(FIntVector(4, 2, 1)));
	TestTrue(TEXT("Embedding: cells round it stay"), Left.Contains(FIntVector(0, 1, 1)) && Left.Contains(FIntVector(2, 1, 2)));
	return true;
}

#endif
