// Copyright Max Harris
//
// Voxel | Cell Properties: open sides, surrounded, neighbourhood, depth, column, components; chunks expand; metadata kept.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelCellProperties.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelCellPropertiesTests
{
	using namespace PCGVoxelTestHelpers;

	UPCGVoxelCellPropertiesSettings* AllOn()
	{
		UPCGVoxelCellPropertiesSettings* Settings = NewObject<UPCGVoxelCellPropertiesSettings>();
		Settings->bWriteOpenNormal = true;
		Settings->OpenNormalSpace = EPCGVoxelDirectionSpace::Grid;
		Settings->bWriteOpenAxisCount = true;
		Settings->bWriteNeighborCount = true;
		Settings->bWriteDepth = true;
		Settings->bWriteCellsAbove = true;
		Settings->bWriteIsSkyVisible = true;
		Settings->bWriteComponentID = true;
		Settings->bWriteComponentSize = true;
		Settings->bWriteCellIndex = true;
		return Settings;
	}

	const UPCGBasePointData* Properties(UPCGVoxelCellPropertiesSettings* Settings, const UPCGData* Voxels)
	{
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelCellPropertiesElement>(Settings, { { PCGPinConstants::DefaultInputLabel, Voxels } });
		return Outputs.IsEmpty() ? nullptr : Cast<UPCGBasePointData>(Outputs[0].Data);
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Default)
	{
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Base || Base->GetTypeId() != PCG::Private::MetadataTypes<T>::Id)
		{
			return Default;
		}
		return static_cast<const FPCGMetadataAttribute<T>*>(Base)->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]);
	}

	/** The output point sitting on Cell (100-unit cells at the origin), or INDEX_NONE. */
	int32 PointAt(const UPCGBasePointData* Points, const FIntVector& Cell)
	{
		const auto Transforms = Points->GetConstTransformValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			if (Transforms[Index].GetLocation().Equals(FVector(Cell) * 100.0, 0.01))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCellOpenSidesTest,
	"PCGUtils.Voxel.CellProperties.OpenSidesOfABlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCellOpenSidesTest::RunTest(const FString&)
{
	using namespace PCGVoxelCellPropertiesTests;

	const UPCGBasePointData* Out = Properties(AllOn(), MakeVoxels(Block(3, 3, 3)));
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	UTEST_EQUAL(TEXT("One point per cell"), Out->GetNumPoints(), 27);

	// The centre: nothing open.
	const int32 Centre = PointAt(Out, FIntVector(1, 1, 1));
	TestTrue(TEXT("Centre is surrounded"), ValueOf<bool>(Out, TEXT("IsSurrounded"), Centre, false));
	TestEqual(TEXT("Centre has no open sides"), ValueOf<int32>(Out, TEXT("OpenSides"), Centre, -1), 0);
	TestEqual(TEXT("Centre has 26 neighbours"), ValueOf<int32>(Out, TEXT("NeighborCount"), Centre, -1), 26);
	TestEqual(TEXT("Centre is depth 2"), ValueOf<int32>(Out, TEXT("Depth"), Centre, -1), 2);
	TestEqual(TEXT("Centre is inside (0 open axes)"), ValueOf<int32>(Out, TEXT("OpenAxisCount"), Centre, -1), 0);
	TestTrue(TEXT("Centre's open normal is zero"), ValueOf<FVector>(Out, TEXT("OpenNormal"), Centre, FVector(9.0)).IsZero());

	// The low corner: -X, -Y, -Z open.
	const int32 Corner = PointAt(Out, FIntVector(0, 0, 0));
	TestFalse(TEXT("Corner is not surrounded"), ValueOf<bool>(Out, TEXT("IsSurrounded"), Corner, true));
	TestEqual(TEXT("Corner mask is -X | -Y | -Z (2 + 8 + 32)"), ValueOf<int32>(Out, TEXT("OpenSides"), Corner, -1), 42);
	TestEqual(TEXT("Corner has 3 open sides"), ValueOf<int32>(Out, TEXT("OpenSideCount"), Corner, -1), 3);
	TestEqual(TEXT("Corner positive open sides"), ValueOf<FVector>(Out, TEXT("PositiveOpenSides"), Corner, FVector(9.0)), FVector(0.0));
	TestEqual(TEXT("Corner negative open sides"), ValueOf<FVector>(Out, TEXT("NegativeOpenSides"), Corner, FVector(9.0)), FVector(-1.0));
	TestEqual(TEXT("Corner is on 3 open axes"), ValueOf<int32>(Out, TEXT("OpenAxisCount"), Corner, -1), 3);
	TestEqual(TEXT("Corner has 7 neighbours"), ValueOf<int32>(Out, TEXT("NeighborCount"), Corner, -1), 7);
	TestTrue(TEXT("Corner's open normal points out of the corner"),
		ValueOf<FVector>(Out, TEXT("OpenNormal"), Corner, FVector(0.0)).Equals(FVector(-1.0).GetSafeNormal(), 1e-6));
	TestEqual(TEXT("Corner is depth 1"), ValueOf<int32>(Out, TEXT("Depth"), Corner, -1), 1);
	TestEqual(TEXT("Corner's cell index"), ValueOf<FVector>(Out, TEXT("CellIndex"), Corner, FVector(9.0)), FVector(0.0));

	// The top face centre: only +Z open.
	const int32 Top = PointAt(Out, FIntVector(1, 1, 2));
	TestEqual(TEXT("Top centre mask is +Z (16)"), ValueOf<int32>(Out, TEXT("OpenSides"), Top, -1), 16);
	TestEqual(TEXT("Top centre positive open sides"), ValueOf<FVector>(Out, TEXT("PositiveOpenSides"), Top, FVector(9.0)), FVector(0, 0, 1));
	TestEqual(TEXT("Top centre negative open sides"), ValueOf<FVector>(Out, TEXT("NegativeOpenSides"), Top, FVector(9.0)), FVector(0.0));
	TestTrue(TEXT("Top centre sees the sky"), ValueOf<bool>(Out, TEXT("IsSkyVisible"), Top, false));
	TestFalse(TEXT("The centre below it does not"), ValueOf<bool>(Out, TEXT("IsSkyVisible"), Centre, true));
	TestEqual(TEXT("The bottom middle has 2 cells above"), ValueOf<int32>(Out, TEXT("CellsAbove"), PointAt(Out, FIntVector(1, 1, 0)), -1), 2);

	// Metadata and the default outputs only.
	TestEqual(TEXT("Points keep their Id"), ValueOf<int32>(Out, TEXT("Id"), Centre, -1), 13);
	const UPCGBasePointData* Defaults = Properties(NewObject<UPCGVoxelCellPropertiesSettings>(), MakeVoxels(Block(2, 1, 1)));
	UTEST_NOT_NULL(TEXT("Defaults ran"), Defaults);
	const FPCGMetadataDomain* Domain = Defaults->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
	TestTrue(TEXT("Defaults write OpenSides"), Domain->HasAttribute(TEXT("OpenSides")));
	TestFalse(TEXT("Defaults do not write Depth"), Domain->HasAttribute(TEXT("Depth")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCellColumnsAndComponentsTest,
	"PCGUtils.Voxel.CellProperties.ColumnsAndComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCellColumnsAndComponentsTest::RunTest(const FString&)
{
	using namespace PCGVoxelCellPropertiesTests;

	// A column with a gap at z = 2, and a cell touching the column's base only at an edge.
	const TArray<FIntVector> Cells = { FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 3), FIntVector(1, 1, 0) };
	UPCGVoxelCellPropertiesSettings* Settings = AllOn();
	const UPCGBasePointData* Out = Properties(Settings, MakeVoxels(Cells));
	UTEST_NOT_NULL(TEXT("Out exists"), Out);
	TestEqual(TEXT("z0 has one cell above before the gap"), ValueOf<int32>(Out, TEXT("CellsAbove"), PointAt(Out, Cells[0]), -1), 1);
	TestEqual(TEXT("z1 has none"), ValueOf<int32>(Out, TEXT("CellsAbove"), PointAt(Out, Cells[1]), -1), 0);
	TestFalse(TEXT("z1 does not see the sky (z3 is above)"), ValueOf<bool>(Out, TEXT("IsSkyVisible"), PointAt(Out, Cells[1]), true));
	TestTrue(TEXT("z3 sees the sky"), ValueOf<bool>(Out, TEXT("IsSkyVisible"), PointAt(Out, Cells[2]), false));

	// Faces: the base pair, the floating z3 cell and the edge-touching cell are three components.
	TestEqual(TEXT("Faces: base pair is component 0"), ValueOf<int32>(Out, TEXT("ComponentID"), PointAt(Out, Cells[0]), -1), 0);
	TestEqual(TEXT("Faces: base pair has 2 cells"), ValueOf<int32>(Out, TEXT("ComponentSize"), PointAt(Out, Cells[1]), -1), 2);
	TestEqual(TEXT("Faces: the edge cell is alone"), ValueOf<int32>(Out, TEXT("ComponentSize"), PointAt(Out, Cells[3]), -1), 1);
	TestEqual(TEXT("Faces: the floating cell is the last component"), ValueOf<int32>(Out, TEXT("ComponentID"), PointAt(Out, Cells[2]), -1), 2);

	Settings->Connectivity = EPCGVoxelConnectivity::Edges;
	Out = Properties(Settings, MakeVoxels(Cells));
	UTEST_NOT_NULL(TEXT("Out exists (edges)"), Out);
	TestEqual(TEXT("Edges: the edge cell joins the base"), ValueOf<int32>(Out, TEXT("ComponentSize"), PointAt(Out, Cells[3]), -1), 3);
	TestEqual(TEXT("Edges: the floating cell stays alone"), ValueOf<int32>(Out, TEXT("ComponentSize"), PointAt(Out, Cells[2]), -1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCellChunksTest,
	"PCGUtils.Voxel.CellProperties.ChunksExpandAndMatchPlainCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCellChunksTest::RunTest(const FString&)
{
	using namespace PCGVoxelCellPropertiesTests;

	// A 2x2x2 chunk plus one plain cell beside it, against the same eight + one cells unchunked.
	const TArray<FIntVector> Firsts = { FIntVector(0, 0, 0), FIntVector(2, 0, 0) };
	const TArray<FIntVector> Spans = { FIntVector(2, 2, 2), FIntVector(1, 1, 1) };
	TArray<FIntVector> PlainCells = Block(2, 2, 2);
	PlainCells.Add(FIntVector(2, 0, 0));

	UPCGVoxelCellPropertiesSettings* Settings = AllOn();
	const UPCGBasePointData* Chunked = Properties(Settings, MakeVoxels(Firsts, 100.0, &Spans));
	const UPCGBasePointData* Plain = Properties(Settings, MakeVoxels(PlainCells));
	UTEST_NOT_NULL(TEXT("Chunked ran"), Chunked);
	UTEST_NOT_NULL(TEXT("Plain ran"), Plain);
	UTEST_EQUAL(TEXT("The chunk comes out as one point per cell"), Chunked->GetNumPoints(), 9);
	TestFalse(TEXT("The span attribute is gone"),
		Chunked->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->HasAttribute(TEXT("VoxelSpan")));

	const FName IntNames[] = { TEXT("OpenSides"), TEXT("NeighborCount"), TEXT("Depth"), TEXT("CellsAbove"), TEXT("ComponentSize") };
	bool bSame = true;
	for (const FIntVector& Cell : PlainCells)
	{
		const int32 A = PointAt(Chunked, Cell);
		const int32 B = PointAt(Plain, Cell);
		bSame &= A != INDEX_NONE && B != INDEX_NONE;
		for (const FName Name : IntNames)
		{
			bSame &= ValueOf<int32>(Chunked, Name, A, -1) == ValueOf<int32>(Plain, Name, B, -2);
		}
	}
	TestTrue(TEXT("Every cell gets the same properties chunked or plain"), bSame);
	TestEqual(TEXT("Cells of the chunk keep the chunk's Id"), ValueOf<int32>(Chunked, TEXT("Id"), PointAt(Chunked, FIntVector(1, 1, 1)), -1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCellConcaveTest,
	"PCGUtils.Voxel.CellProperties.InsideCornersAreConcave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCellConcaveTest::RunTest(const FString&)
{
	using namespace PCGVoxelCellPropertiesTests;

	UPCGVoxelCellPropertiesSettings* Settings = NewObject<UPCGVoxelCellPropertiesSettings>();
	Settings->bWriteIsInnerCorner = true;
	Settings->bWriteConcaveEdges = true;
	Settings->bWriteConcaveCorners = true;
	Settings->bWriteConcaveCount = true;
	Settings->bWriteConcaveDirection = true;
	Settings->ConcaveDirectionSpace = EPCGVoxelDirectionSpace::Grid;

	// A 3x3x3 block with its +X+Y+Z corner cell removed: a one-cell notch.
	TArray<FIntVector> Notched = Block(3, 3, 3);
	Notched.Remove(FIntVector(2, 2, 2));
	const UPCGBasePointData* Out = Properties(Settings, MakeVoxels(Notched));
	UTEST_NOT_NULL(TEXT("Notched ran"), Out);

	const int32 Centre = PointAt(Out, FIntVector(1, 1, 1));
	TestTrue(TEXT("Notch: the centre is an inner corner"), ValueOf<bool>(Out, TEXT("IsInnerCorner"), Centre, false));
	TestEqual(TEXT("Notch: the centre has no concave edge"), ValueOf<int32>(Out, TEXT("ConcaveEdges"), Centre, -1), 0);
	TestEqual(TEXT("Notch: the centre's concave corner is +X+Y+Z (1)"), ValueOf<int32>(Out, TEXT("ConcaveCorners"), Centre, -1), 1);
	TestEqual(TEXT("Notch: the centre's concave count"), ValueOf<int32>(Out, TEXT("ConcaveCount"), Centre, -1), 1);
	TestTrue(TEXT("Notch: the centre's direction points into the notch"),
		ValueOf<FVector>(Out, TEXT("ConcaveDirection"), Centre, FVector(0.0)).Equals(FVector(1.0).GetSafeNormal(), 1e-6));

	// (2, 1, 1) is open on +X but sits in the +Y+Z crease under the notch.
	const int32 Side = PointAt(Out, FIntVector(2, 1, 1));
	TestFalse(TEXT("Notch: an open cell is never an inner corner"), ValueOf<bool>(Out, TEXT("IsInnerCorner"), Side, true));
	TestEqual(TEXT("Notch: the side cell's concave edge is +Y+Z (256)"), ValueOf<int32>(Out, TEXT("ConcaveEdges"), Side, -1), 256);
	TestEqual(TEXT("Notch: a far corner has nothing concave"), ValueOf<int32>(Out, TEXT("ConcaveCount"), PointAt(Out, FIntVector(0, 0, 0)), -1), 0);

	// The same block with the whole +X+Y column removed: a crease along Z.
	TArray<FIntVector> Creased = Block(3, 3, 3);
	for (int32 Z = 0; Z < 3; ++Z)
	{
		Creased.Remove(FIntVector(2, 2, Z));
	}
	Out = Properties(Settings, MakeVoxels(Creased));
	UTEST_NOT_NULL(TEXT("Creased ran"), Out);
	const int32 Middle = PointAt(Out, FIntVector(1, 1, 1));
	TestTrue(TEXT("Crease: the centre is an inner corner"), ValueOf<bool>(Out, TEXT("IsInnerCorner"), Middle, false));
	TestEqual(TEXT("Crease: the centre's concave edge is +X+Y (1)"), ValueOf<int32>(Out, TEXT("ConcaveEdges"), Middle, -1), 1);
	TestEqual(TEXT("Crease: corners behind an empty edge are not concave corners"),
		ValueOf<int32>(Out, TEXT("ConcaveCorners"), Middle, -1), 0);
	TestTrue(TEXT("Crease: the direction points into the crease"),
		ValueOf<FVector>(Out, TEXT("ConcaveDirection"), Middle, FVector(0.0)).Equals(FVector(1, 1, 0).GetSafeNormal(), 1e-6));

	Out = Properties(Settings, MakeVoxels(Block(3, 3, 3)));
	UTEST_NOT_NULL(TEXT("Block ran"), Out);
	TestFalse(TEXT("A plain block's centre is not an inner corner"),
		ValueOf<bool>(Out, TEXT("IsInnerCorner"), PointAt(Out, FIntVector(1, 1, 1)), true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelCellNamesTest,
	"PCGUtils.Voxel.CellProperties.EnabledOutputsNeedTheirOwnNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelCellNamesTest::RunTest(const FString&)
{
	using namespace PCGVoxelCellPropertiesTests;

	UPCGVoxelCellPropertiesSettings* Settings = NewObject<UPCGVoxelCellPropertiesSettings>();
	Settings->OpenSideCountAttribute = Settings->OpenSidesAttribute;
	AddExpectedError(TEXT("are both 'OpenSides'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("Two enabled outputs with one name produce nothing"), Properties(Settings, MakeVoxels(Block(2, 1, 1))));

	Settings->OpenSideCountAttribute = NAME_None;
	AddExpectedError(TEXT("Open Side Count Attribute is empty"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("An enabled output with no name produces nothing"), Properties(Settings, MakeVoxels(Block(2, 1, 1))));

	Settings->bWriteOpenSideCount = false;
	TestNotNull(TEXT("Turning that output off is fine"), Properties(Settings, MakeVoxels(Block(2, 1, 1))));
	return true;
}

#endif
