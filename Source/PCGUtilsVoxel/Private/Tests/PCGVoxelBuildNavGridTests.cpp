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
		Settings->bEnableCompression = false; // the cases below count cells; Compression has its own test
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelBuildNavGridCompressionTest, "PCGUtils.Voxel.BuildNavGrid.Compression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGVoxelBuildNavGridCompressionTest::RunTest(const FString&)
{
	using namespace PCGVoxelBuildNavGridTests;
	auto Compressed = [](UPCGVoxelBuildNavGridSettings& S)
	{
		S.bEnableCompression = true;
		S.bIgnoreGaps = false; // the tunnel and pocket are thinner than a gap
		S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
		S.SurfaceClearance.Constant = 0.0;
	};
	// Sum of the cells centre vertices stand for, the portals (IsPortal) and the hub points (neither).
	auto Centres = [](const FResult& R, int64& OutCells, int32& OutPortals, int32& OutHub)
	{
		OutCells = 0;
		OutPortals = 0;
		OutHub = 0;
		const FPCGMetadataAttribute<FVector>* Size = Attr<FVector>(R.Vtx, TEXT("ChunkSize"));
		const FPCGMetadataAttribute<bool>* Portal = Attr<bool>(R.Vtx, TEXT("IsPortal"));
		const auto Entries = R.Vtx->GetConstMetadataEntryValueRange();
		for (int32 K = 0; Size && Portal && K < R.Vtx->GetNumPoints(); ++K)
		{
			const FVector S = Size->GetValueFromItemKey(Entries[K]);
			OutCells += FMath::RoundToInt64(S.X * S.Y * S.Z);
			const bool bPortal = Portal->GetValueFromItemKey(Entries[K]);
			OutPortals += bPortal ? 1 : 0;
			OutHub += (!bPortal && S.IsZero()) ? 1 : 0;
		}
	};
	// Every edge, sampled finely, stays in cells IsFree accepts (a point on a cell boundary needs both cells free).
	auto EdgesStayFree = [](const FResult& R, TFunctionRef<bool(const FIntVector&)> IsFree)
	{
		const auto Vtx = R.Vtx->GetConstTransformValueRange();
		for (const UPCGBasePointData* E : R.Edges)
		{
			const FPCGMetadataAttribute<int64>* EData = Attr<int64>(E, PCGUtilsClusterInterop::EdgeDataAttribute);
			const auto Entries = E->GetConstMetadataEntryValueRange();
			for (int32 K = 0; EData && K < E->GetNumPoints(); ++K)
			{
				const uint64 V = static_cast<uint64>(EData->GetValueFromItemKey(Entries[K]));
				const FVector A = Vtx[static_cast<int32>(V >> 32)].GetLocation();
				const FVector B = Vtx[static_cast<int32>(V & 0xffffffffu)].GetLocation();
				for (int32 Step = 0; Step <= 64; ++Step)
				{
					const FVector P = FMath::Lerp(A, B, Step / 64.0) / 100.0;
					for (int32 Corner = 0; Corner < 8; ++Corner)
					{
						const FVector Q = P + FVector((Corner & 1) ? 1e-4 : -1e-4, (Corner & 2) ? 1e-4 : -1e-4, (Corner & 4) ? 1e-4 : -1e-4);
						if (!IsFree(FIntVector(FMath::FloorToInt32(Q.X + 0.5), FMath::FloorToInt32(Q.Y + 0.5), FMath::FloorToInt32(Q.Z + 0.5))))
						{
							return false;
						}
					}
				}
			}
		}
		return true;
	};

	// A 1 x 1 x 10 tunnel: boxes of 4, 4 and 2 along it with the same faces, so centre to centre and no portal.
	{
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < 3; ++Z)
			for (int32 Y = 0; Y < 3; ++Y)
				for (int32 X = 0; X < 12; ++X)
				{
					if (!(Y == 1 && Z == 1 && X >= 1 && X <= 10))
					{
						Cells.Add(FIntVector(X, Y, Z));
					}
				}
		const FResult R = Build(MakeVoxels(Cells), Compressed);
		TestEqual(TEXT("Tunnel: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 3);
		TestEqual(TEXT("Tunnel: edges"), R.NumEdges(), 2);
		TestEqual(TEXT("Tunnel: cluster"), CheckCluster(R), FString());
		if (R.Vtx && R.Vtx->GetNumPoints() == 3)
		{
			TArray<double> Xs;
			for (const FTransform& T : R.Vtx->GetConstTransformValueRange())
			{
				Xs.Add(T.GetLocation().X);
			}
			Xs.Sort();
			TestEqual(TEXT("Tunnel: first box centre"), Xs[0], 250.0, 1e-6);
			TestEqual(TEXT("Tunnel: second box centre"), Xs[1], 650.0, 1e-6);
			TestEqual(TEXT("Tunnel: last box centre"), Xs[2], 950.0, 1e-6);
			const FPCGMetadataAttribute<double>* Distance = Attr<double>(R.Vtx, TEXT("SurfaceDistance"));
			const auto Entries = R.Vtx->GetConstMetadataEntryValueRange();
			for (int32 K = 0; Distance && K < 3; ++K)
			{
				TestEqual(TEXT("Tunnel: half a cell from the walls"), Distance->GetValueFromItemKey(Entries[K]), 50.0, 1e-6);
			}
		}
	}
	// A sealed 2 x 2 x 2 pocket is one box touching nothing: split in two so it stays a cluster.
	{
		const FResult R = Build(MakeVoxels(Shell(4)), Compressed);
		TestEqual(TEXT("Pocket: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 2);
		TestEqual(TEXT("Pocket: edges"), R.NumEdges(), 1);
		TestEqual(TEXT("Pocket: cluster"), CheckCluster(R), FString());
	}
	// A room with a full height pillar: every free cell in exactly one box, portals between unequal faces, every edge
	// inside free space, with no hub cubes, one, two, and one subdivided.
	{
		TArray<FIntVector> Cells = Shell(9);
		for (int32 Z = 1; Z <= 7; ++Z)
		{
			Cells.Add(FIntVector(4, 4, Z));
		}
		auto IsFree = [](const FIntVector& C)
		{
			return C.X >= 1 && C.Y >= 1 && C.Z >= 1 && C.X <= 7 && C.Y <= 7 && C.Z <= 7 && !(C.X == 4 && C.Y == 4);
		};
		const FIntVector2 Hubs[] = { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 1, 2 } };
		int32 PlainEdges = 0, PlainVertices = 0;
		for (const FIntVector2& Hub : Hubs)
		{
			const FResult R = Build(MakeVoxels(Cells), [&](UPCGVoxelBuildNavGridSettings& S)
			{
				Compressed(S);
				S.HubCubes = Hub.X;
				S.HubSubdivisions = Hub.Y;
			});
			const FString Label = FString::Printf(TEXT("Pillar, %d hub cubes, %d subdivisions"), Hub.X, Hub.Y);
			TestEqual(Label + TEXT(": cluster"), CheckCluster(R), FString());
			TestEqual(Label + TEXT(": one island"), R.Edges.Num(), 1);
			if (!R.Vtx)
			{
				continue;
			}
			int64 Covered = 0;
			int32 Portals = 0, HubPoints = 0;
			Centres(R, Covered, Portals, HubPoints);
			TestEqual(Label + TEXT(": boxes cover every free cell"), Covered, int64(7 * 7 * 7 - 7));
			TestTrue(Label + TEXT(": has portals"), Portals > 0);
			TestTrue(Label + TEXT(": edges stay in free space"), EdgesStayFree(R, IsFree));
			TestTrue(Label + TEXT(": IsAerial written"), Attr<bool>(R.Vtx, TEXT("IsAerial")) != nullptr);
			if (Hub.X == 0)
			{
				TestEqual(Label + TEXT(": no hub points"), HubPoints, 0);
				TestTrue(Label + TEXT(": fewer vertices than cells"), R.Vtx->GetNumPoints() < 7 * 7 * 7 - 7);
				PlainEdges = R.NumEdges();
				PlainVertices = R.Vtx->GetNumPoints();
			}
			else
			{
				TestTrue(Label + TEXT(": hub points"), HubPoints > 0);
				TestTrue(Label + TEXT(": hubs add edges"), R.NumEdges() > PlainEdges);
				TestEqual(Label + TEXT(": hubs keep the portals and centres"), R.Vtx->GetNumPoints() - HubPoints, PlainVertices);
			}
		}
	}
	// One hub cube in a 5 x 5 x 5 box with a portal on each of two opposite faces: 8 corners halfway to the box's
	// extent, joined along the cube's 12 sides and to the centre, each face-centre portal joining the 4 corners facing it.
	{
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < 8; ++Z)
			for (int32 Y = 0; Y < 7; ++Y)
				for (int32 X = 0; X < 9; ++X)
				{
					const bool bRoom = X >= 1 && X <= 5 && Y >= 1 && Y <= 5 && Z >= 1 && Z <= 5;
					const bool bStubs = Y == 3 && Z == 3 && (X == 6 || X == 7); // a 1 x 1 x 2 corridor off the +X face
					const bool bStub2 = Y == 3 && X == 3 && Z == 6; // and a 1 cell nook off the +Z face
					if (!bRoom && !bStubs && !bStub2)
					{
						Cells.Add(FIntVector(X, Y, Z));
					}
				}
		const FResult R = Build(MakeVoxels(Cells), [](UPCGVoxelBuildNavGridSettings& S)
		{
			S.bEnableCompression = true;
			S.bIgnoreGaps = false;
			S.FlyingSpace = EPCGVoxelNavFlyingSpace::EnclosedOnly;
			S.SurfaceClearance.Constant = 0.0;
			S.CompressionSize.Constant = 8.0;
			S.bCollapseAerialCells = false;
			S.AerialCellDistance = 0; // every cell aerial, so the room is one box
		});
		TestEqual(TEXT("Hub: cluster"), CheckCluster(R), FString());
		int64 Covered = 0;
		int32 Portals = 0, HubPoints = 0;
		if (R.Vtx)
		{
			Centres(R, Covered, Portals, HubPoints);
		}
		TestEqual(TEXT("Hub: two portals"), Portals, 2);
		TestEqual(TEXT("Hub: eight corners"), HubPoints, 8);
		// Room centre + 8 corners + 2 portals + corridor centre + nook centre.
		TestEqual(TEXT("Hub: vertices"), R.Vtx ? R.Vtx->GetNumPoints() : -1, 13);
		// 12 sides + 8 to the centre + each portal: its two centres and the corners nearest it (4 for X: the +X face of
		// the cube; the +Z nook portal at (3, 3, 5.5) is nearest the 4 top corners).
		TestEqual(TEXT("Hub: edges"), R.NumEdges(), 12 + 8 + 2 * (2 + 4));
		if (R.Vtx)
		{
			const auto Transforms = R.Vtx->GetConstTransformValueRange();
			const FPCGMetadataAttribute<FVector>* Size = Attr<FVector>(R.Vtx, TEXT("ChunkSize"));
			const FPCGMetadataAttribute<bool>* Portal = Attr<bool>(R.Vtx, TEXT("IsPortal"));
			const auto Entries = R.Vtx->GetConstMetadataEntryValueRange();
			for (int32 K = 0; Size && Portal && K < R.Vtx->GetNumPoints(); ++K)
			{
				if (!Portal->GetValueFromItemKey(Entries[K]) && Size->GetValueFromItemKey(Entries[K]).IsZero())
				{
					// Centre (300, 300, 300), extent 2 cells: corners 1 cell out on every axis.
					const FVector Offset = (Transforms[K].GetLocation() - FVector(300.0)).GetAbs();
					TestEqual(TEXT("Hub: corner halfway"), Offset, FVector(100.0), 1e-3f);
				}
			}
		}
	}
	// An open room: the 3 x 3 x 3 middle is aerial (2 cells from every wall) and collapses to one box.
	{
		const FResult R = Build(MakeVoxels(Shell(9)), Compressed);
		TestEqual(TEXT("Open room: cluster"), CheckCluster(R), FString());
		int32 AerialCentres = 0;
		if (R.Vtx)
		{
			const FPCGMetadataAttribute<bool>* Aerial = Attr<bool>(R.Vtx, TEXT("IsAerial"));
			const FPCGMetadataAttribute<FVector>* Size = Attr<FVector>(R.Vtx, TEXT("ChunkSize"));
			const auto Entries = R.Vtx->GetConstMetadataEntryValueRange();
			for (int32 K = 0; Aerial && Size && K < R.Vtx->GetNumPoints(); ++K)
			{
				const FVector S = Size->GetValueFromItemKey(Entries[K]);
				if (Aerial->GetValueFromItemKey(Entries[K]) && !S.IsZero())
				{
					++AerialCentres;
					TestEqual(TEXT("Open room: the aerial box is the middle"), S, FVector(3.0), 1e-4f);
				}
			}
		}
		TestEqual(TEXT("Open room: one aerial box"), AerialCentres, 1);
	}
	return true;
}

#endif
