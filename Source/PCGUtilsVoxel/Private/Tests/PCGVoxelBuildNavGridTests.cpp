// Copyright Max Harris
//
// Voxel | Build Nav Grid: flying vertices where the agent fits with clearance, face (and diagonal) edges, islands as
// separate Edges datas, and a well formed PCGEx cluster (tags, VData degrees, EData endpoints).

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGPointArrayData.h"
#include "Data/PCGUtilsClusterInterop.h"
#include "Elements/PCGVoxelBuildNavGrid.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelBuildNavGridTests
{
	using namespace PCGVoxelTestHelpers;

	struct FResult
	{
		const UPCGBasePointData* Vtx = nullptr;
		TArray<FString> VtxTags;
		TArray<const UPCGBasePointData*> Edges;
		TArray<TArray<FString>> EdgeTags;
		int32 NumEdges() const
		{
			int32 Sum = 0;
			for (const UPCGBasePointData* E : Edges) { Sum += E->GetNumPoints(); }
			return Sum;
		}
	};

	FResult Build(const UPCGData* In, TFunctionRef<void(UPCGVoxelBuildNavGridSettings&)> Edit, const UPCGData* Bounds = nullptr)
	{
		UPCGVoxelBuildNavGridSettings* Settings = NewObject<UPCGVoxelBuildNavGridSettings>();
		Settings->AgentSize.Constant = FVector(1.0);
		Edit(*Settings);
		TArray<TPair<FName, const UPCGData*>> Inputs = { { PCGVoxelBuildNavGridPins::VoxelGrid, In } };
		if (Bounds)
		{
			Inputs.Add({ PCGVoxelBuildNavGridPins::Bounds, Bounds });
		}
		FResult Result;
		for (const FPCGTaggedData& Data : Run<FPCGVoxelBuildNavGridElement>(Settings, Inputs))
		{
			if (Data.Pin == PCGVoxelBuildNavGridPins::Vtx)
			{
				Result.Vtx = Cast<UPCGBasePointData>(Data.Data);
				Result.VtxTags = Data.Tags.Array();
			}
			else
			{
				Result.Edges.Add(Cast<UPCGBasePointData>(Data.Data));
				Result.EdgeTags.Add(Data.Tags.Array());
			}
		}
		return Result;
	}

	/** A hollow cube: Outer cells per side, walls Wall thick, starting at Origin. */
	TArray<FIntVector> Shell(int32 Outer, int32 Wall = 1, const FIntVector& Origin = FIntVector::ZeroValue)
	{
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < Outer; ++Z)
			for (int32 Y = 0; Y < Outer; ++Y)
				for (int32 X = 0; X < Outer; ++X)
				{
					const bool bInside = X >= Wall && Y >= Wall && Z >= Wall && X < Outer - Wall && Y < Outer - Wall && Z < Outer - Wall;
					if (!bInside)
					{
						Cells.Add(Origin + FIntVector(X, Y, Z));
					}
				}
		return Cells;
	}

	template <typename T>
	const FPCGMetadataAttribute<T>* Attr(const UPCGBasePointData* Points, FName Name)
	{
		return Points->ConstMetadata()->GetConstTypedAttribute<T>(Name);
	}

	/** Checks the PCGEx contract; returns an empty string when the cluster is well formed. */
	FString CheckCluster(const FResult& R)
	{
		using namespace PCGUtilsClusterInterop;
		if (!R.Vtx)
		{
			return TEXT("no Vtx output");
		}
		if (!R.VtxTags.Contains(VtxTag))
		{
			return TEXT("Vtx is not tagged PCGEx/Vtx");
		}
		FString Pair;
		for (const FString& Tag : R.VtxTags)
		{
			if (Tag.StartsWith(ClusterPairTagKey + TEXT(":")))
			{
				Pair = Tag;
			}
		}
		if (Pair.IsEmpty())
		{
			return TEXT("Vtx has no pair tag");
		}
		const FPCGMetadataAttribute<int64>* VData = Attr<int64>(R.Vtx, VtxDataAttribute);
		if (!VData)
		{
			return TEXT("Vtx has no PCGEx/VData");
		}
		const int32 NumVtx = R.Vtx->GetNumPoints();
		TArray<int32> Expected, Seen;
		Expected.Init(0, NumVtx);
		Seen.Init(0, NumVtx);
		const auto VtxEntries = R.Vtx->GetConstMetadataEntryValueRange();
		for (int32 K = 0; K < NumVtx; ++K)
		{
			const uint64 V = static_cast<uint64>(VData->GetValueFromItemKey(VtxEntries[K]));
			const int32 Id = static_cast<int32>(V >> 32);
			if (Id != K)
			{
				return FString::Printf(TEXT("vertex %d has id %d"), K, Id);
			}
			Expected[K] = static_cast<int32>(V & 0xffffffffu);
		}
		for (int32 E = 0; E < R.Edges.Num(); ++E)
		{
			if (!R.EdgeTags[E].Contains(EdgesTag) || !R.EdgeTags[E].Contains(Pair))
			{
				return FString::Printf(TEXT("Edges %d is missing the PCGEx/Edges or pair tag"), E);
			}
			const FPCGMetadataAttribute<int64>* EData = Attr<int64>(R.Edges[E], EdgeDataAttribute);
			if (!EData)
			{
				return FString::Printf(TEXT("Edges %d has no PCGEx/EData"), E);
			}
			const auto Entries = R.Edges[E]->GetConstMetadataEntryValueRange();
			for (int32 K = 0; K < R.Edges[E]->GetNumPoints(); ++K)
			{
				const uint64 V = static_cast<uint64>(EData->GetValueFromItemKey(Entries[K]));
				const int32 A = static_cast<int32>(V >> 32);
				const int32 B = static_cast<int32>(V & 0xffffffffu);
				if (A < 0 || B < 0 || A >= NumVtx || B >= NumVtx || A == B)
				{
					return FString::Printf(TEXT("Edges %d edge %d joins %d and %d"), E, K, A, B);
				}
				++Seen[A];
				++Seen[B];
			}
		}
		for (int32 K = 0; K < NumVtx; ++K)
		{
			if (Seen[K] != Expected[K] || Seen[K] == 0)
			{
				return FString::Printf(TEXT("vertex %d expects %d edges and has %d"), K, Expected[K], Seen[K]);
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBuildNavGridFlyingTest, "PCGUtils.Voxel.BuildNavGrid.Flying",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBuildNavGridFlyingTest::RunTest(const FString&)
{
	using namespace PCGVoxelBuildNavGridTests;
	const UPCGPointArrayData* Room = MakeVoxels(Shell(9));

	// A sealed 7 wide room; clearance 1 keeps off the layer next to the walls: 5 x 5 x 5 vertices, 300 face edges.
	{
		const FResult R = Build(Room, [](UPCGVoxelBuildNavGridSettings& S) { S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly; });
		TestEqual(TEXT("Room: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 125);
		TestEqual(TEXT("Room: edges"), R.NumEdges(), 300);
		TestEqual(TEXT("Room: one island"), R.Edges.Num(), 1);
		TestEqual(TEXT("Room: cluster"), CheckCluster(R), FString());
		if (R.Vtx)
		{
			// Nearest wall is 2 cells from the outermost vertices' centres: 1.5 cells to its face.
			const FPCGMetadataAttribute<double>* Distance = Attr<double>(R.Vtx, TEXT("SurfaceDistance"));
			double Lowest = TNumericLimits<double>::Max();
			const auto Entries = R.Vtx->GetConstMetadataEntryValueRange();
			for (int32 K = 0; Distance && K < R.Vtx->GetNumPoints(); ++K)
			{
				Lowest = FMath::Min(Lowest, Distance->GetValueFromItemKey(Entries[K]));
			}
			TestTrue(TEXT("Room: surface distance written"), Distance != nullptr);
			TestEqual(TEXT("Room: nearest wall 150 away"), Lowest, 150.0, 1e-6);
		}
	}
	// Clearance 0: the whole 7 x 7 x 7 inside.
	{
		const FResult R = Build(Room, [](UPCGVoxelBuildNavGridSettings& S)
		{
			S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
			S.SurfaceClearance.Constant = 0.0;
		});
		TestEqual(TEXT("Clearance 0: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 343);
		TestEqual(TEXT("Clearance 0: cluster"), CheckCluster(R), FString());
	}
	// Agent two tall: 5 x 5 footprint, 4 heights, vertices half a cell up.
	{
		const FResult R = Build(Room, [](UPCGVoxelBuildNavGridSettings& S)
		{
			S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
			S.AgentSize.Constant = FVector(1.0, 1.0, 2.0);
		});
		TestEqual(TEXT("Tall agent: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 100);
		if (R.Vtx && R.Vtx->GetNumPoints() > 0)
		{
			const double Z = R.Vtx->GetConstTransformValueRange()[0].GetLocation().Z;
			TestEqual(TEXT("Tall agent: vertex between two cells"), FMath::Fmod(Z + 1000.0, 100.0), 50.0, 1e-6);
			TestEqual(TEXT("Tall agent: bounds are the agent"), R.Vtx->GetConstBoundsMaxValueRange()[0].Z, 100.0, 1e-6);
		}
		TestEqual(TEXT("Tall agent: cluster"), CheckCluster(R), FString());
	}
	// Diagonals in the 5 x 5 x 5 block: 300 face + 480 edge + 256 corner moves.
	{
		const FResult R = Build(Room, [](UPCGVoxelBuildNavGridSettings& S)
		{
			S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
			S.bEnableDiagonals = true;
		});
		TestEqual(TEXT("Diagonals: edges"), R.NumEdges(), 1036);
		TestEqual(TEXT("Diagonals: cluster"), CheckCluster(R), FString());
	}
	// Outside a single cell, padding 2, clearance 0: the 5 x 5 x 5 box less the cell.
	{
		const FResult R = Build(MakeVoxels({ FIntVector(0) }), [](UPCGVoxelBuildNavGridSettings& S) { S.SurfaceClearance.Constant = 0.0; });
		TestEqual(TEXT("Padded box: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 124);
		TestEqual(TEXT("Padded box: cluster"), CheckCluster(R), FString());
	}
	// Outside Only round a room: the room's inside is left out.
	{
		const FResult R = Build(Room, [](UPCGVoxelBuildNavGridSettings& S)
		{
			S.FlyingSpace = EPCGVoxelNavFlyingSpace::OutsideOnly;
			S.SurfaceClearance.Constant = 0.0;
			S.Padding = 1;
		});
		TestEqual(TEXT("Outside only: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 11 * 11 * 11 - 9 * 9 * 9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBuildNavGridIslandsTest, "PCGUtils.Voxel.BuildNavGrid.Islands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBuildNavGridIslandsTest::RunTest(const FString&)
{
	using namespace PCGVoxelBuildNavGridTests;
	// Two sealed rooms side by side: one Vtx data, two Edges datas sharing its pair tag.
	TArray<FIntVector> Cells = Shell(7);
	Cells.Append(Shell(7, 1, FIntVector(10, 0, 0)));
	const FResult R = Build(MakeVoxels(Cells), [](UPCGVoxelBuildNavGridSettings& S)
	{
		S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
		S.SurfaceClearance.Constant = 0.0;
	});
	TestEqual(TEXT("Two rooms: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 2 * 125);
	TestEqual(TEXT("Two rooms: islands"), R.Edges.Num(), 2);
	TestEqual(TEXT("Two rooms: cluster"), CheckCluster(R), FString());

	// Per point clearance: one room's points ask for 1, the other's 0.
	UPCGPointArrayData* Voxels = MakeVoxels(Cells);
	FPCGMetadataAttribute<double>* Clearance = Voxels->MutableMetadata()->CreateAttribute<double>(TEXT("SurfaceClearance"), 0.0, false, false);
	const auto Entries = Voxels->GetConstMetadataEntryValueRange();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		Clearance->SetValue(Entries[Index], Cells[Index].X >= 10 ? 1.0 : 0.0);
	}
	const FResult P = Build(Voxels, [](UPCGVoxelBuildNavGridSettings& S)
	{
		S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
		S.SurfaceClearance.Input = EPCGUtilsPointInputValueType::Attribute;
	});
	TestEqual(TEXT("Per point clearance: vertices"), P.Vtx ? P.Vtx->GetNumPoints() : -1, 125 + 27);
	TestEqual(TEXT("Per point clearance: cluster"), CheckCluster(P), FString());

	// Bounds: a box over the first room's lower half only.
	UPCGPointArrayData* Box = NewObject<UPCGPointArrayData>();
	Box->SetNumPoints(1);
	Box->GetTransformValueRange()[0] = FTransform(FVector(300.0, 300.0, 150.0));
	Box->GetBoundsMinValueRange()[0] = FVector(-250.0, -250.0, -100.0);
	Box->GetBoundsMaxValueRange()[0] = FVector(250.0, 250.0, 100.0);
	const FResult B = Build(MakeVoxels(Cells), [](UPCGVoxelBuildNavGridSettings& S) { S.SurfaceClearance.Constant = 0.0; }, Box);
	// Centres 100 to 500 on X and Y, 100 and 200 on Z: the room's inside rows z = 1 and 2, 5 x 5 each.
	TestEqual(TEXT("Bounds: vertices"), B.Vtx ? B.Vtx->GetNumPoints() : -1, 50);
	TestEqual(TEXT("Bounds: cluster"), CheckCluster(B), FString());
	return true;
}

#endif
