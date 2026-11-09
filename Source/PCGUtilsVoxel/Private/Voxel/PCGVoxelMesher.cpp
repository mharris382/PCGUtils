// Copyright Max Harris

#include "Voxel/PCGVoxelMesher.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Algo/Sort.h"
#include "Algo/StableSort.h"
#include "Async/ParallelFor.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelMesherPrivate
{
	using UE::Geometry::FDynamicMesh3;

	// A direction index d: axis d / 2, positive when d is even.
	int32 DirAxis(int32 Dir) { return Dir / 2; }
	int32 DirSign(int32 Dir) { return (Dir % 2 == 0) ? 1 : -1; }
	int32 MakeDir(int32 Axis, int32 Sign) { return Axis * 2 + (Sign > 0 ? 0 : 1); }
	FVector DirVector(int32 Dir)
	{
		FVector V(0.0);
		V[DirAxis(Dir)] = DirSign(Dir);
		return V;
	}

	/** The two in-plane axes of a face on Axis, in cyclic order so that U x V = Axis. */
	int32 PlaneU(int32 Axis) { return (Axis + 1) % 3; }
	int32 PlaneV(int32 Axis) { return (Axis + 2) % 3; }

	// Face corners in canonical (u, v) order: counter-clockwise seen from the positive axis.
	constexpr int32 CornerDU[4] = { 0, 1, 1, 0 };
	constexpr int32 CornerDV[4] = { 0, 0, 1, 1 };

	/**
	 * Which way is outward for the engine: the sign of A . (B x C) summed over the engine's own box. Read once, so
	 * the winding below follows the engine rather than a convention remembered here.
	 */
	bool MathCrossIsOutward()
	{
		static const bool bOutward = []
		{
			UE::Geometry::FMinimalBoxMeshGenerator Generator;
			Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(1.0));
			Generator.Generate();
			const FDynamicMesh3 Box(&Generator);
			double Six = 0.0;
			for (const int32 TriangleID : Box.TriangleIndicesItr())
			{
				FVector3d A, B, C;
				Box.GetTriVertices(TriangleID, A, B, C);
				Six += A.Dot(B.Cross(C));
			}
			return Six > 0.0;
		}();
		return bOutward;
	}

	struct FFace
	{
		FIntVector Cell;
		int32 Dir = 0;
		int32 Point = INDEX_NONE;
		int32 Region = INDEX_NONE;
	};

	struct FRegion
	{
		int32 Dir = 0;
		int32 Plane = 0;
		int32 U0 = 0, V0 = 0, U1 = 0, V1 = 0;
		int32 RepFace = INDEX_NONE;
	};

	FIntVector CornerLattice(const FFace& Face, int32 Corner)
	{
		const int32 Axis = DirAxis(Face.Dir);
		FIntVector Lattice = Face.Cell;
		if (DirSign(Face.Dir) > 0)
		{
			Lattice[Axis] += 1;
		}
		Lattice[PlaneU(Axis)] += CornerDU[Corner];
		Lattice[PlaneV(Axis)] += CornerDV[Corner];
		return Lattice;
	}

	/** Face (u, v) projected to texture (U, V) so side faces stay upright. Coordinates in cells. */
	FVector2d Project(const FVector& Cells, int32 Dir)
	{
		switch (Dir)
		{
		case 0: return FVector2d(Cells.Y, -Cells.Z);   // +X
		case 1: return FVector2d(-Cells.Y, -Cells.Z);  // -X
		case 2: return FVector2d(-Cells.X, -Cells.Z);  // +Y
		case 3: return FVector2d(Cells.X, -Cells.Z);   // -Y
		case 4: return FVector2d(Cells.X, -Cells.Y);   // +Z
		default: return FVector2d(Cells.X, Cells.Y);   // -Z
		}
	}

	struct FUnionFind
	{
		TArray<int32> Parent;
		explicit FUnionFind(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Parent[Index] = Index;
			}
		}
		int32 Find(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};
}

void PCGVoxelMesher::BuildMesh(const FPCGVoxelMeshInput& Input, const FPCGVoxelMeshOptions& Options,
	UE::Geometry::FDynamicMesh3& OutMesh, FPCGVoxelMeshStats* OutStats)
{
	using namespace PCGVoxelMesherPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGVoxelMesher::BuildMesh);

	if (Options.bBevel)
	{
		BuildBeveledMesh(Input, Options, OutMesh, OutStats);
		return;
	}
	OutMesh.Clear();
	if (!Input.Grid || Input.Grid->NumCells() == 0)
	{
		return;
	}
	const FPCGVoxelGrid& Grid = *Input.Grid;
	double StageStart = FPlatformTime::Seconds();
	auto EndStage = [&](int32 Stage)
	{
		const double Now = FPlatformTime::Seconds();
		if (OutStats)
		{
			OutStats->StageSeconds[Stage] += Now - StageStart;
		}
		StageStart = Now;
	};

	const bool bColors = Input.Color.Num() == Grid.NumPoints();
	const bool bMaterials = Input.MaterialSlot.Num() == Grid.NumPoints();
	const bool bOffsets = Input.OffsetMin.Num() == Grid.NumPoints() && Input.OffsetMax.Num() == Grid.NumPoints();
	const bool bAtlas = Options.UVMode == EPCGVoxelUVMode::AtlasFromAttribute && Input.AtlasRect.Num() == Grid.NumPoints();
	const bool bCollapse = Options.bCollapseCoplanarFaces && Options.UVMode != EPCGVoxelUVMode::AtlasFromAttribute;

	auto SlotOf = [&](int32 Point) { return bMaterials ? Input.MaterialSlot[Point] : 0; };
	auto OffsetOf = [&](int32 Point, int32 Dir) -> double
	{
		if (!bOffsets)
		{
			return 0.0;
		}
		return DirSign(Dir) > 0 ? Input.OffsetMax[Point][DirAxis(Dir)] : Input.OffsetMin[Point][DirAxis(Dir)];
	};

	// ---- 1. Faces: every cell side whose neighbour is empty. A cell's faces are stored together, in direction
	// order, so face (cell, direction) is found from the cell's first face and a 6-bit mask, with no hash lookup.
	const int32 NumCells = Grid.NumCells();
	TArray<uint8> FaceMask;
	FaceMask.SetNumUninitialized(NumCells);
	ParallelForTemplate(TEXT("PCGVoxelMesher.FaceMask"), NumCells, 4096, [&](int32 CellIndex)
	{
		const FIntVector& Cell = Grid.Cells[CellIndex];
		uint8 Mask = 0;
		for (int32 Dir = 0; Dir < 6; ++Dir)
		{
			FIntVector Neighbour = Cell;
			Neighbour[DirAxis(Dir)] += DirSign(Dir);
			if (!Grid.IsOccupied(Neighbour))
			{
				Mask |= 1 << Dir;
			}
		}
		FaceMask[CellIndex] = Mask;
	});
	TArray<int32> FaceBase;
	FaceBase.SetNumUninitialized(NumCells + 1);
	FaceBase[0] = 0;
	for (int32 CellIndex = 0; CellIndex < NumCells; ++CellIndex)
	{
		FaceBase[CellIndex + 1] = FaceBase[CellIndex] + FMath::CountBits(FaceMask[CellIndex]);
	}
	const int32 NumFaces = FaceBase[NumCells];
	TArray<FFace> Faces;
	Faces.SetNumUninitialized(NumFaces);
	ParallelForTemplate(TEXT("PCGVoxelMesher.Faces"), NumCells, 4096, [&](int32 CellIndex)
	{
		int32 FaceID = FaceBase[CellIndex];
		for (int32 Dir = 0; Dir < 6; ++Dir)
		{
			if (FaceMask[CellIndex] & (1 << Dir))
			{
				Faces[FaceID++] = { Grid.Cells[CellIndex], Dir, Grid.CellPoint[CellIndex], INDEX_NONE };
			}
		}
	});
	auto FaceOfCell = [&](int32 CellIndex, int32 Dir) -> int32
	{
		const uint32 Mask = FaceMask[CellIndex];
		if (!(Mask & (1u << Dir)))
		{
			return INDEX_NONE;
		}
		return FaceBase[CellIndex] + FMath::CountBits(Mask & ((1u << Dir) - 1u));
	};
	auto FaceAt = [&](const FIntVector& Cell, int32 Dir) -> int32
	{
		const int32 CellIndex = Grid.FindCellIndex(Cell);
		return CellIndex == INDEX_NONE ? INDEX_NONE : FaceOfCell(CellIndex, Dir);
	};
	EndStage(0);

	// ---- 2. Vertices: join the corners of faces that continue each other across an edge. Where cells touch only
	// along an edge, each face continues onto its own cell's faces, which splits the vertices there.
	// For each face edge, the corner uses of the partner face that meet this face's two corners on that edge
	// (INDEX_NONE when there is no partner). Found in parallel; joined serially below.
	TArray<FIntPoint> EdgeJoin;
	EdgeJoin.SetNumUninitialized(NumFaces * 4);
	ParallelForTemplate(TEXT("PCGVoxelMesher.EdgeJoin"), NumFaces, 4096, [&](int32 FaceID)
	{
		const FFace& Face = Faces[FaceID];
		const int32 Axis = DirAxis(Face.Dir);
		FIntVector OutOfFace(0);
		OutOfFace[Axis] = DirSign(Face.Dir);
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			// Edge 0 runs along u at v = 0, so it leaves the face towards -v; and so on round the face.
			const int32 TAxis = (Edge == 0 || Edge == 2) ? PlaneV(Axis) : PlaneU(Axis);
			const int32 TSign = (Edge == 0 || Edge == 3) ? -1 : 1;
			FIntVector Across(0);
			Across[TAxis] = TSign;
			const FIntVector Side = Face.Cell + Across;
			int32 Partner = INDEX_NONE;
			const int32 SideCell = Grid.FindCellIndex(Side);
			if (SideCell == INDEX_NONE)
			{
				Partner = FaceAt(Face.Cell, MakeDir(TAxis, TSign));        // convex: round the cell's own edge
			}
			else
			{
				const int32 DiagonalCell = Grid.FindCellIndex(Side + OutOfFace);
				Partner = DiagonalCell == INDEX_NONE
					? FaceOfCell(SideCell, Face.Dir)                       // flat: the next cell's face
					: FaceOfCell(DiagonalCell, MakeDir(TAxis, -TSign));    // concave: up the wall in front
			}
			FIntPoint Join(INDEX_NONE, INDEX_NONE);
			if (Partner != INDEX_NONE)
			{
				for (int32 Which = 0; Which < 2; ++Which)
				{
					const FIntVector Lattice = CornerLattice(Face, (Edge + Which) % 4);
					for (int32 Other = 0; Other < 4; ++Other)
					{
						if (CornerLattice(Faces[Partner], Other) == Lattice)
						{
							Join[Which] = Partner * 4 + Other;
							break;
						}
					}
				}
			}
			EdgeJoin[FaceID * 4 + Edge] = Join;
		}
	});

	FUnionFind Corners(NumFaces * 4);
	for (int32 FaceEdge = 0; FaceEdge < NumFaces * 4; ++FaceEdge)
	{
		const FIntPoint Join = EdgeJoin[FaceEdge];
		const int32 FaceID = FaceEdge / 4;
		const int32 Edge = FaceEdge % 4;
		if (Join.X != INDEX_NONE)
		{
			Corners.Union(FaceID * 4 + Edge, Join.X);
		}
		if (Join.Y != INDEX_NONE)
		{
			Corners.Union(FaceID * 4 + (Edge + 1) % 4, Join.Y);
		}
	}
	EdgeJoin.Empty();

	TArray<int32> VertexOfCorner;
	VertexOfCorner.SetNumUninitialized(NumFaces * 4);
	TArray<int32> RootToVertex;
	RootToVertex.Init(INDEX_NONE, NumFaces * 4);
	TArray<FIntVector> VertexLattice;
	VertexLattice.Reserve(NumFaces);
	for (int32 Use = 0; Use < NumFaces * 4; ++Use)
	{
		const int32 Root = Corners.Find(Use);
		if (RootToVertex[Root] == INDEX_NONE)
		{
			RootToVertex[Root] = VertexLattice.Num();
			VertexLattice.Add(CornerLattice(Faces[Use / 4], Use % 4));
		}
		VertexOfCorner[Use] = RootToVertex[Root];
	}
	const int32 NumVertices = VertexLattice.Num();
	RootToVertex.Empty();
	Corners.Parent.Empty();
	EndStage(1);

	// ---- 3. Vertex positions in grid space, moved by the surface offset: along each face normal meeting at the
	// vertex, by the average offset of the faces with that normal.
	TArray<FVector> VertexGrid;
	VertexGrid.SetNumUninitialized(NumVertices);
	for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
	{
		VertexGrid[Vertex] = (FVector(VertexLattice[Vertex]) - FVector(0.5)) * Grid.VoxelSize;
	}
	if (bOffsets)
	{
		TArray<double> Sum;
		TArray<int32> Count;
		Sum.Init(0.0, NumVertices * 6);
		Count.Init(0, NumVertices * 6);
		for (int32 Use = 0; Use < NumFaces * 4; ++Use)
		{
			const FFace& Face = Faces[Use / 4];
			Sum[VertexOfCorner[Use] * 6 + Face.Dir] += OffsetOf(Face.Point, Face.Dir);
			++Count[VertexOfCorner[Use] * 6 + Face.Dir];
		}
		for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
		{
			for (int32 Dir = 0; Dir < 6; ++Dir)
			{
				if (const int32 N = Count[Vertex * 6 + Dir])
				{
					VertexGrid[Vertex] += DirVector(Dir) * (Sum[Vertex * 6 + Dir] / N);
				}
			}
		}
	}
	EndStage(2);

	// ---- 4. Smooth colors: the average over the distinct cells meeting at a vertex (within a material when asked).
	// Face corners are bucketed by vertex; each vertex's entries (one per material, or one) are stored from
	// VertexUseStart[Vertex], SmoothCount[Vertex] of them.
	const bool bSmooth = bColors && Options.ColorTransition != EPCGVoxelColorTransition::Hard;
	const bool bByMaterial = Options.ColorTransition == EPCGVoxelColorTransition::SmoothWithinMaterial;
	auto ColorSub = [&](int32 Point) { return bByMaterial ? SlotOf(Point) : 0; };
	struct FSmoothEntry { int32 Sub; FVector4f Color; };
	TArray<int32> VertexUseStart;
	TArray<uint8> SmoothCount;
	TArray<FSmoothEntry> SmoothEntries;
	if (bSmooth)
	{
		VertexUseStart.Init(0, NumVertices + 1);
		for (int32 Use = 0; Use < NumFaces * 4; ++Use)
		{
			++VertexUseStart[VertexOfCorner[Use] + 1];
		}
		for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
		{
			VertexUseStart[Vertex + 1] += VertexUseStart[Vertex];
		}
		TArray<int32> Bucket;
		Bucket.SetNumUninitialized(NumFaces * 4);
		{
			TArray<int32> Cursor(VertexUseStart.GetData(), NumVertices);
			for (int32 Use = 0; Use < NumFaces * 4; ++Use)
			{
				Bucket[Cursor[VertexOfCorner[Use]]++] = Use;
			}
		}
		SmoothCount.SetNumZeroed(NumVertices);
		SmoothEntries.SetNumUninitialized(NumFaces * 4);
		ParallelForTemplate(TEXT("PCGVoxelMesher.SmoothColors"), NumVertices, 4096, [&](int32 Vertex)
		{
			const int32 Begin = VertexUseStart[Vertex];
			const int32 End = VertexUseStart[Vertex + 1];
			TArray<FIntVector, TInlineAllocator<16>> SeenCells;
			TArray<int32, TInlineAllocator<16>> Counts;
			int32 NumEntries = 0;
			for (int32 Index = Begin; Index < End; ++Index)
			{
				const FFace& Face = Faces[Bucket[Index] / 4];
				// A cell counts once however many of its faces meet here.
				if (SeenCells.Contains(Face.Cell))
				{
					continue;
				}
				SeenCells.Add(Face.Cell);
				const int32 Sub = ColorSub(Face.Point);
				int32 Entry = 0;
				while (Entry < NumEntries && SmoothEntries[Begin + Entry].Sub != Sub)
				{
					++Entry;
				}
				if (Entry == NumEntries)
				{
					SmoothEntries[Begin + Entry] = { Sub, FVector4f(0.0f, 0.0f, 0.0f, 0.0f) };
					Counts.Add(0);
					++NumEntries;
				}
				SmoothEntries[Begin + Entry].Color += Input.Color[Face.Point];
				++Counts[Entry];
			}
			for (int32 Entry = 0; Entry < NumEntries; ++Entry)
			{
				SmoothEntries[Begin + Entry].Color /= static_cast<float>(Counts[Entry]);
			}
			SmoothCount[Vertex] = static_cast<uint8>(NumEntries);
		});
	}
	/** The smooth entry (an index into SmoothEntries) of a vertex for a material sub-group. */
	auto SmoothEntryOf = [&](int32 Vertex, int32 Sub) -> int32
	{
		const int32 Begin = VertexUseStart[Vertex];
		for (int32 Entry = 0; Entry < SmoothCount[Vertex]; ++Entry)
		{
			if (SmoothEntries[Begin + Entry].Sub == Sub)
			{
				return Begin + Entry;
			}
		}
		checkNoEntry();
		return Begin;
	};
	auto CornerColor = [&](int32 FaceID, int32 Corner) -> FVector4f
	{
		const FFace& Face = Faces[FaceID];
		if (!bSmooth)
		{
			return Input.Color[Face.Point];
		}
		return SmoothEntries[SmoothEntryOf(VertexOfCorner[FaceID * 4 + Corner], ColorSub(Face.Point))].Color;
	};
	EndStage(3);

	// ---- 5. Regions: one per face, or maximal rectangles of equal faces in one plane when collapsing.
	auto CellOnPlane = [](int32 Axis, int32 Plane, int32 U, int32 V)
	{
		FIntVector Cell;
		Cell[Axis] = Plane;
		Cell[PlaneU(Axis)] = U;
		Cell[PlaneV(Axis)] = V;
		return Cell;
	};
	auto FaceOnPlane = [&](int32 Dir, int32 Plane, int32 U, int32 V)
	{
		return FaceAt(CellOnPlane(DirAxis(Dir), Plane, U, V), Dir);
	};

	TArray<FRegion> Regions;
	TArray<bool> VertexKept;
	if (!bCollapse)
	{
		Regions.SetNumUninitialized(NumFaces);
		ParallelForTemplate(TEXT("PCGVoxelMesher.Regions"), NumFaces, 8192, [&](int32 FaceID)
		{
			FFace& Face = Faces[FaceID];
			const int32 Axis = DirAxis(Face.Dir);
			FRegion& Region = Regions[FaceID];
			Region.Dir = Face.Dir;
			Region.Plane = Face.Cell[Axis];
			Region.U0 = Region.U1 = Face.Cell[PlaneU(Axis)];
			Region.V0 = Region.V1 = Face.Cell[PlaneV(Axis)];
			Region.RepFace = FaceID;
			Face.Region = FaceID;
		});
	}
	else
	{
		// Smooth colors collapse only where a face is one flat color, so gradients survive.
		TArray<bool> FaceUniform;
		if (bSmooth)
		{
			FaceUniform.SetNumUninitialized(NumFaces);
			ParallelForTemplate(TEXT("PCGVoxelMesher.FaceUniform"), NumFaces, 8192, [&](int32 FaceID)
			{
				const FVector4f First = CornerColor(FaceID, 0);
				FaceUniform[FaceID] = CornerColor(FaceID, 1) == First && CornerColor(FaceID, 2) == First && CornerColor(FaceID, 3) == First;
			});
		}
		auto SameFace = [&](int32 A, int32 B) -> bool
		{
			const FFace& FaceA = Faces[A];
			const FFace& FaceB = Faces[B];
			if (FaceA.Point == FaceB.Point && !bSmooth)
			{
				return true;
			}
			if (SlotOf(FaceA.Point) != SlotOf(FaceB.Point) || OffsetOf(FaceA.Point, FaceA.Dir) != OffsetOf(FaceB.Point, FaceB.Dir))
			{
				return false;
			}
			if (Options.Polygroups == EPCGVoxelPolygroupMode::PerAttributeValue && !Input.Polygroup.IsEmpty()
				&& Input.Polygroup[FaceA.Point] != Input.Polygroup[FaceB.Point])
			{
				return false;
			}
			if (bColors)
			{
				if (bSmooth)
				{
					if (!FaceUniform[A] || !FaceUniform[B] || CornerColor(A, 0) != CornerColor(B, 0))
					{
						return false;
					}
				}
				else if (Input.Color[FaceA.Point] != Input.Color[FaceB.Point])
				{
					return false;
				}
			}
			return true;
		};

		// Group faces by (direction, plane). Faces are made in cell order (Z, Y, X), so a stable grouping leaves each
		// plane's faces in row order, which is the order the sweep wants. Planes never share a face, so they are
		// swept in parallel and their regions joined in plane order, which keeps the result deterministic.
		const FIntVector Extent = Grid.MaxCell - Grid.MinCell + FIntVector(1);
		auto GroupKeyOf = [&](const FFace& Face) -> int64
		{
			const int32 Axis = DirAxis(Face.Dir);
			return static_cast<int64>(Face.Dir) * (static_cast<int64>(TNumericLimits<int32>::Max()) + 1)
				+ (static_cast<int64>(Face.Cell[Axis]) - Grid.MinCell[Axis]);
		};
		TArray<int32> Grouped;          // face IDs, grouped
		TArray<int32> GroupStart;       // start of each non-empty group in Grouped, plus the end
		Grouped.SetNumUninitialized(NumFaces);
		const int64 MaxExtent = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
		if (MaxExtent * 6 <= static_cast<int64>(NumFaces) * 4 + 1024)
		{
			// Counting sort over every possible (direction, plane) slot.
			auto SlotOf6 = [&](const FFace& Face) { const int32 Axis = DirAxis(Face.Dir); return Face.Dir * static_cast<int32>(MaxExtent) + (Face.Cell[Axis] - Grid.MinCell[Axis]); };
			TArray<int32> Count;
			Count.Init(0, static_cast<int32>(MaxExtent) * 6 + 1);
			for (const FFace& Face : Faces)
			{
				++Count[SlotOf6(Face) + 1];
			}
			for (int32 Slot = 0; Slot < Count.Num() - 1; ++Slot)
			{
				if (Count[Slot + 1] > 0)
				{
					GroupStart.Add(Count[Slot]);
				}
				Count[Slot + 1] += Count[Slot];
			}
			GroupStart.Add(NumFaces);
			for (int32 FaceID = 0; FaceID < NumFaces; ++FaceID)
			{
				Grouped[Count[SlotOf6(Faces[FaceID])]++] = FaceID;
			}
		}
		else
		{
			// Sparse sets with a huge extent: a stable sort by group instead.
			for (int32 FaceID = 0; FaceID < NumFaces; ++FaceID)
			{
				Grouped[FaceID] = FaceID;
			}
			Algo::StableSort(Grouped, [&](int32 A, int32 B) { return GroupKeyOf(Faces[A]) < GroupKeyOf(Faces[B]); });
			for (int32 Index = 0; Index < NumFaces; ++Index)
			{
				if (Index == 0 || GroupKeyOf(Faces[Grouped[Index]]) != GroupKeyOf(Faces[Grouped[Index - 1]]))
				{
					GroupStart.Add(Index);
				}
			}
			GroupStart.Add(NumFaces);
		}

		const int32 NumGroups = GroupStart.Num() - 1;
		TArray<TArray<FRegion>> GroupRegions;
		GroupRegions.SetNum(NumGroups);
		ParallelForTemplate(TEXT("PCGVoxelMesher.Collapse"), NumGroups, 8, [&](int32 GroupIndex)
		{
			TArray<FRegion>& Local = GroupRegions[GroupIndex];
			for (int32 Index = GroupStart[GroupIndex]; Index < GroupStart[GroupIndex + 1]; ++Index)
			{
				const int32 Seed = Grouped[Index];
				const FFace& SeedFace = Faces[Seed];
				if (SeedFace.Region != INDEX_NONE)
				{
					continue;
				}
				const int32 Axis = DirAxis(SeedFace.Dir);
				const int32 Dir = SeedFace.Dir;
				const int32 Plane = SeedFace.Cell[Axis];
				auto Free = [&](int32 U, int32 V) -> bool
				{
					const int32 FaceID = FaceOnPlane(Dir, Plane, U, V);
					return FaceID != INDEX_NONE && Faces[FaceID].Region == INDEX_NONE && SameFace(Seed, FaceID);
				};
				FRegion Region;
				Region.Dir = Dir;
				Region.Plane = Plane;
				Region.U0 = Region.U1 = SeedFace.Cell[PlaneU(Axis)];
				Region.V0 = Region.V1 = SeedFace.Cell[PlaneV(Axis)];
				Region.RepFace = Seed;
				while (Free(Region.U1 + 1, Region.V0))
				{
					++Region.U1;
				}
				for (;;)
				{
					bool bRowFree = true;
					for (int32 U = Region.U0; U <= Region.U1 && bRowFree; ++U)
					{
						bRowFree = Free(U, Region.V1 + 1);
					}
					if (!bRowFree)
					{
						break;
					}
					++Region.V1;
				}
				const int32 LocalID = Local.Add(Region);
				for (int32 V = Region.V0; V <= Region.V1; ++V)
				{
					for (int32 U = Region.U0; U <= Region.U1; ++U)
					{
						Faces[FaceOnPlane(Dir, Plane, U, V)].Region = LocalID;
					}
				}
			}
		});
		TArray<int32> RegionOffset;
		RegionOffset.SetNumUninitialized(NumGroups);
		int32 NumRegions = 0;
		for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
		{
			RegionOffset[GroupIndex] = NumRegions;
			NumRegions += GroupRegions[GroupIndex].Num();
		}
		Regions.SetNumUninitialized(NumRegions);
		ParallelForTemplate(TEXT("PCGVoxelMesher.CollapseJoin"), NumGroups, 8, [&](int32 GroupIndex)
		{
			FMemory::Memcpy(Regions.GetData() + RegionOffset[GroupIndex], GroupRegions[GroupIndex].GetData(), GroupRegions[GroupIndex].Num() * sizeof(FRegion));
			for (int32 Index = GroupStart[GroupIndex]; Index < GroupStart[GroupIndex + 1]; ++Index)
			{
				Faces[Grouped[Index]].Region += RegionOffset[GroupIndex];
			}
		});

		// A vertex survives only when it is a corner of some region. Every region whose outline passes through such
		// a vertex keeps it, so neighbours share every vertex along their common edges and there are no T-junctions;
		// any other vertex lies inside a region or along straight edges only, and is dropped everywhere.
		VertexKept.Init(false, NumVertices);
		for (const FRegion& Region : Regions)
		{
			VertexKept[VertexOfCorner[FaceOnPlane(Region.Dir, Region.Plane, Region.U0, Region.V0) * 4 + 0]] = true;
			VertexKept[VertexOfCorner[FaceOnPlane(Region.Dir, Region.Plane, Region.U1, Region.V0) * 4 + 1]] = true;
			VertexKept[VertexOfCorner[FaceOnPlane(Region.Dir, Region.Plane, Region.U1, Region.V1) * 4 + 2]] = true;
			VertexKept[VertexOfCorner[FaceOnPlane(Region.Dir, Region.Plane, Region.U0, Region.V1) * 4 + 3]] = true;
		}
	}
	EndStage(4);

	// ---- 6. Emit, in three passes: each region's outline and triangles (parallel), the bare mesh (serial: this is
	// the engine's own cost, and it is lowest with no attributes attached yet), then the attributes (serial, into
	// flat arrays).
	const FTransform& GridToWorld = Grid.GridToWorld;
	auto ToOutput = [&](const FVector& GridPosition)
	{
		return Options.OutputSpace.InverseTransformPosition(GridToWorld.TransformPositionNoScale(GridPosition));
	};
	const bool bMirrored = Options.OutputSpace.GetDeterminant() < 0.0;
	const bool bFlip = (!MathCrossIsOutward()) != bMirrored;
	const int32 NumRegions = Regions.Num();

	// 6a. Outlines: corner vertices in canonical order (counter-clockwise in u, v). A single face is its four
	// corners; a collapsed region keeps its four corners and every edge vertex another region uses.
	auto WalkOutline = [&](const FRegion& Region, auto&& Visit)
	{
		if (Region.U0 == Region.U1 && Region.V0 == Region.V1)
		{
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Visit(VertexOfCorner[Region.RepFace * 4 + Corner]);
			}
			return;
		}
		auto At = [&](int32 LU, int32 LV)
		{
			const int32 FU = FMath::Min(LU, Region.U1);
			const int32 FV = FMath::Min(LV, Region.V1);
			const int32 DU = LU - FU;
			const int32 DV = LV - FV;
			const int32 Corner = DV == 0 ? (DU == 0 ? 0 : 1) : (DU == 0 ? 3 : 2);
			const int32 Vertex = VertexOfCorner[FaceOnPlane(Region.Dir, Region.Plane, FU, FV) * 4 + Corner];
			const bool bCorner = (LU == Region.U0 || LU == Region.U1 + 1) && (LV == Region.V0 || LV == Region.V1 + 1);
			if (bCorner || VertexKept[Vertex])
			{
				Visit(Vertex);
			}
		};
		for (int32 U = Region.U0; U <= Region.U1 + 1; ++U) At(U, Region.V0);
		for (int32 V = Region.V0 + 1; V <= Region.V1 + 1; ++V) At(Region.U1 + 1, V);
		for (int32 U = Region.U1; U >= Region.U0; --U) At(U, Region.V1 + 1);
		for (int32 V = Region.V1; V >= Region.V0 + 1; --V) At(Region.U0, V);
	};

	TArray<int32> PolyStart;
	PolyStart.SetNumUninitialized(NumRegions + 1);
	if (!bCollapse)
	{
		for (int32 RegionID = 0; RegionID <= NumRegions; ++RegionID)
		{
			PolyStart[RegionID] = RegionID * 4;
		}
	}
	else
	{
		ParallelForTemplate(TEXT("PCGVoxelMesher.OutlineCount"), NumRegions, 1024, [&](int32 RegionID)
		{
			int32 Count = 0;
			WalkOutline(Regions[RegionID], [&](int32) { ++Count; });
			PolyStart[RegionID + 1] = Count;
		});
		PolyStart[0] = 0;
		for (int32 RegionID = 0; RegionID < NumRegions; ++RegionID)
		{
			PolyStart[RegionID + 1] += PolyStart[RegionID];
		}
	}
	const int32 NumSlots = PolyStart[NumRegions];

	// Triangles of region R live in slots PolyStart[R] .. PolyStart[R] + TriCount[R] as local corner indices (a
	// corner index equal to the outline length means the region's centre point). An outline of N corners gives
	// N - 2 triangles, or N when it falls back to a fan round its centre, so N slots are always enough.
	TArray<int32> PolyVertex;
	PolyVertex.SetNumUninitialized(NumSlots);
	TArray<FIntVector> TriCorner;
	TriCorner.SetNumUninitialized(NumSlots);
	TArray<int32> TriCount;
	TriCount.SetNumUninitialized(NumRegions);
	TArray<bool> NeedsCentre;
	NeedsCentre.Init(false, NumRegions);
	ParallelForTemplate(TEXT("PCGVoxelMesher.Triangulate"), NumRegions, 1024, [&](int32 RegionID)
	{
		const FRegion& Region = Regions[RegionID];
		const int32 Base = PolyStart[RegionID];
		const int32 N = PolyStart[RegionID + 1] - Base;
		int32 Fill = Base;
		WalkOutline(Region, [&](int32 Vertex) { PolyVertex[Fill++] = Vertex; });

		// Canonical order faces +axis by the math cross product; reverse it for -axis faces and for the engine.
		const bool bReverse = (DirSign(Region.Dir) < 0) != bFlip;
		int32 NumTris = 0;
		auto AddTri = [&](int32 A, int32 B, int32 C)
		{
			TriCorner[Base + NumTris++] = bReverse ? FIntVector(A, C, B) : FIntVector(A, B, C);
		};
		if (N == 4)
		{
			AddTri(0, 1, 2);
			AddTri(0, 2, 3);
			TriCount[RegionID] = NumTris;
			return;
		}

		// Ear clipping, which gives N - 2 triangles and never a zero-area one. Points are whole lattice steps in
		// (u, v), so the collinear test is exact.
		const int32 Axis = DirAxis(Region.Dir);
		TArray<FIntPoint, TInlineAllocator<64>> Lattice2D;
		for (int32 Index = 0; Index < N; ++Index)
		{
			const FIntVector& L = VertexLattice[PolyVertex[Base + Index]];
			Lattice2D.Add(FIntPoint(L[PlaneU(Axis)], L[PlaneV(Axis)]));
		}
		TArray<int32, TInlineAllocator<64>> Ring;
		for (int32 Index = 0; Index < N; ++Index)
		{
			Ring.Add(Index);
		}
		auto Cross = [&](int32 A, int32 B, int32 C) -> int64
		{
			const FIntPoint AB = Lattice2D[B] - Lattice2D[A];
			const FIntPoint BC = Lattice2D[C] - Lattice2D[B];
			return static_cast<int64>(AB.X) * BC.Y - static_cast<int64>(AB.Y) * BC.X;
		};
		auto IsFlat = [&](int32 At)
		{
			const int32 Num = Ring.Num();
			return Cross(Ring[(At + Num - 1) % Num], Ring[At], Ring[(At + 1) % Num]) == 0;
		};
		bool bDegenerate = false;
		while (Ring.Num() > 3)
		{
			const int32 Num = Ring.Num();
			// Clip a convex corner next to a flat one first, so the flat ones become corners in turn.
			int32 Pick = INDEX_NONE;
			for (int32 At = 0; At < Num && Pick == INDEX_NONE; ++At)
			{
				if (!IsFlat(At) && (IsFlat((At + Num - 1) % Num) || IsFlat((At + 1) % Num)))
				{
					Pick = At;
				}
			}
			for (int32 At = 0; At < Num && Pick == INDEX_NONE; ++At)
			{
				if (!IsFlat(At))
				{
					Pick = At;
				}
			}
			if (Pick == INDEX_NONE)
			{
				bDegenerate = true;
				break;
			}
			AddTri(Ring[(Pick + Num - 1) % Num], Ring[Pick], Ring[(Pick + 1) % Num]);
			Ring.RemoveAt(Pick, EAllowShrinking::No);
		}
		if (!bDegenerate && Cross(Ring[0], Ring[1], Ring[2]) == 0)
		{
			bDegenerate = true;
		}
		if (!bDegenerate)
		{
			AddTri(Ring[0], Ring[1], Ring[2]);
		}
		else
		{
			// Fallback that cannot fail: a fan round a centre point.
			NumTris = 0;
			for (int32 Index = 0; Index < N; ++Index)
			{
				AddTri(N, Index, (Index + 1) % N);
			}
			NeedsCentre[RegionID] = true;
		}
		TriCount[RegionID] = NumTris;
	});

	auto IsRectCorner = [&](const FRegion& Region, int32 Vertex)
	{
		const int32 Axis = DirAxis(Region.Dir);
		const int32 LU = VertexLattice[Vertex][PlaneU(Axis)];
		const int32 LV = VertexLattice[Vertex][PlaneV(Axis)];
		return (LU == Region.U0 || LU == Region.U1 + 1) && (LV == Region.V0 || LV == Region.V1 + 1);
	};

	// 6b. The bare mesh: vertices (positions in parallel, appended in vertex order) and triangles.
	OutMesh.EnableTriangleGroups();
	TArray<int32> MeshVertex;
	MeshVertex.Init(INDEX_NONE, NumVertices);
	{
		TArray<FVector3d> OutputPosition;
		OutputPosition.SetNumUninitialized(NumVertices);
		ParallelForTemplate(TEXT("PCGVoxelMesher.Positions"), NumVertices, 8192, [&](int32 Vertex)
		{
			OutputPosition[Vertex] = FVector3d(ToOutput(VertexGrid[Vertex]));
		});
		// With collapse only region corners and the edge vertices kept for them are used.
		for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
		{
			if (VertexKept.IsEmpty() || VertexKept[Vertex])
			{
				MeshVertex[Vertex] = OutMesh.AppendVertex(OutputPosition[Vertex]);
			}
		}
	}
	TMap<int32, int32> CentreVertex;     // region -> mesh vertex, for the rare fan fallback
	for (int32 RegionID = 0; RegionID < NumRegions; ++RegionID)
	{
		if (!NeedsCentre[RegionID])
		{
			continue;
		}
		FVector GridSum(0.0);
		int32 NumCorners = 0;
		for (int32 Slot = PolyStart[RegionID]; Slot < PolyStart[RegionID + 1]; ++Slot)
		{
			if (IsRectCorner(Regions[RegionID], PolyVertex[Slot]))
			{
				GridSum += VertexGrid[PolyVertex[Slot]];
				++NumCorners;
			}
		}
		CentreVertex.Add(RegionID, OutMesh.AppendVertex(FVector3d(ToOutput(GridSum / FMath::Max(NumCorners, 1)))));
	}

	auto GroupOf = [&](int32 RegionID) -> int32
	{
		switch (Options.Polygroups)
		{
		case EPCGVoxelPolygroupMode::PerFaceDirection:   return Regions[RegionID].Dir;
		case EPCGVoxelPolygroupMode::PerMaterial:        return SlotOf(Faces[Regions[RegionID].RepFace].Point);
		case EPCGVoxelPolygroupMode::PerCollapsedRegion: return RegionID;
		case EPCGVoxelPolygroupMode::PerAttributeValue:
			return Input.Polygroup.IsEmpty() ? 0 : Input.Polygroup[Faces[Regions[RegionID].RepFace].Point];
		default: return 0;
		}
	};
	TArray<int32> TriangleID;
	TriangleID.SetNumUninitialized(NumSlots);
	for (int32 RegionID = 0; RegionID < NumRegions; ++RegionID)
	{
		const int32 Base = PolyStart[RegionID];
		const int32 N = PolyStart[RegionID + 1] - Base;
		const int32 Group = GroupOf(RegionID);
		auto MeshVertexOf = [&](int32 Corner) { return Corner == N ? CentreVertex.FindChecked(RegionID) : MeshVertex[PolyVertex[Base + Corner]]; };
		for (int32 Tri = 0; Tri < TriCount[RegionID]; ++Tri)
		{
			const FIntVector& C = TriCorner[Base + Tri];
			TriangleID[Base + Tri] = OutMesh.AppendTriangle(MeshVertexOf(C.X), MeshVertexOf(C.Y), MeshVertexOf(C.Z), Group);
		}
	}

	// 6c. Attributes. Overlay elements are shared through flat arrays: by (vertex, direction) for normals, grid
	// UVs and hard colors, by smooth entry for smooth colors.
	OutMesh.EnableAttributes();
	UE::Geometry::FDynamicMeshAttributeSet* Attributes = OutMesh.Attributes();
	Attributes->EnableMaterialID();
	UE::Geometry::FDynamicMeshNormalOverlay* Normals = Attributes->PrimaryNormals();
	UE::Geometry::FDynamicMeshUVOverlay* UVs = nullptr;
	if (Options.UVMode != EPCGVoxelUVMode::None)
	{
		const int32 Channel = FMath::Clamp(Options.UVChannel, 0, 7);
		Attributes->SetNumUVLayers(FMath::Max(Attributes->NumUVLayers(), Channel + 1));
		UVs = Attributes->GetUVLayer(Channel);
	}
	UE::Geometry::FDynamicMeshColorOverlay* Colors = nullptr;
	if (bColors)
	{
		Attributes->EnablePrimaryColors();
		Colors = Attributes->PrimaryColors();
	}
	UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Attributes->GetMaterialID();

	TArray<int32> NormalElement;
	NormalElement.Init(INDEX_NONE, NumVertices * 6);
	TArray<int32> GridUVElement;
	if (UVs && Options.UVMode == EPCGVoxelUVMode::GridProjection)
	{
		GridUVElement.Init(INDEX_NONE, NumVertices * 6);
	}
	TArray<int32> ColorElement;
	TArray<FVector4f> HardColorOfElement;
	if (Colors)
	{
		ColorElement.Init(INDEX_NONE, bSmooth ? SmoothEntries.Num() : NumVertices * 6);
		if (!bSmooth)
		{
			HardColorOfElement.SetNumUninitialized(NumVertices * 6);
		}
	}

	struct FCornerElements { int32 Normal = INDEX_NONE; int32 UV = INDEX_NONE; int32 Color = INDEX_NONE; };
	TArray<FCornerElements, TInlineAllocator<64>> CornerElements;
	int32 NumRegionsEmitted = 0;
	for (int32 RegionID = 0; RegionID < NumRegions; ++RegionID)
	{
		const FRegion& Region = Regions[RegionID];
		const int32 Base = PolyStart[RegionID];
		const int32 N = PolyStart[RegionID + 1] - Base;
		const int32 Point = Faces[Region.RepFace].Point;
		const int32 Slot = SlotOf(Point);
		const FVector3f Normal = FVector3f(DirVector(Region.Dir));
		auto CellsOf = [&](int32 Vertex) { return FVector(VertexLattice[Vertex]) - FVector(0.5); };

		// UV origin: the lowest projected corner of the rectangle.
		FVector2d UVOrigin(TNumericLimits<double>::Max());
		if (UVs && GridUVElement.IsEmpty())
		{
			for (int32 Index = 0; Index < N; ++Index)
			{
				const int32 Vertex = PolyVertex[Base + Index];
				if (IsRectCorner(Region, Vertex))
				{
					const FVector2d Projected = Project(CellsOf(Vertex), Region.Dir);
					UVOrigin = FVector2d(FMath::Min(UVOrigin.X, Projected.X), FMath::Min(UVOrigin.Y, Projected.Y));
				}
			}
		}
		auto MakeUV = [&](const FVector& Cells) -> FVector2f
		{
			const FVector2d Projected = Project(Cells, Region.Dir);
			switch (Options.UVMode)
			{
			case EPCGVoxelUVMode::GridProjection:
				return FVector2f(Projected * Options.UVScale);
			case EPCGVoxelUVMode::PerFace:
				return FVector2f(Projected - UVOrigin);
			case EPCGVoxelUVMode::AtlasFromAttribute:
			{
				const FVector2d Local = Projected - UVOrigin;
				const FVector4f Rect = bAtlas ? Input.AtlasRect[Point] : FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
				return FVector2f(Rect.X + Local.X * (Rect.Z - Rect.X), Rect.Y + Local.Y * (Rect.W - Rect.Y));
			}
			default:
				return FVector2f::ZeroVector;
			}
		};

		CornerElements.SetNum(N + (NeedsCentre[RegionID] ? 1 : 0), EAllowShrinking::No);
		for (int32 Index = 0; Index < N; ++Index)
		{
			const int32 Vertex = PolyVertex[Base + Index];
			FCornerElements& Elements = CornerElements[Index];
			const int32 Key = Vertex * 6 + Region.Dir;
			int32& NormalID = NormalElement[Key];
			if (NormalID == INDEX_NONE)
			{
				NormalID = Normals->AppendElement(Normal);
			}
			Elements.Normal = NormalID;
			if (UVs)
			{
				if (!GridUVElement.IsEmpty())
				{
					int32& UVID = GridUVElement[Key];
					if (UVID == INDEX_NONE)
					{
						UVID = UVs->AppendElement(MakeUV(CellsOf(Vertex)));
					}
					Elements.UV = UVID;
				}
				else
				{
					Elements.UV = UVs->AppendElement(MakeUV(CellsOf(Vertex)));
				}
			}
			if (Colors)
			{
				if (bSmooth)
				{
					const int32 Entry = SmoothEntryOf(Vertex, ColorSub(Point));
					int32& ColorID = ColorElement[Entry];
					if (ColorID == INDEX_NONE)
					{
						ColorID = Colors->AppendElement(SmoothEntries[Entry].Color);
					}
					Elements.Color = ColorID;
				}
				else
				{
					// Shared with the other faces at this vertex and direction that have the same color; a
					// different color starts its own element, which is the hard split.
					const FVector4f& Color = Input.Color[Point];
					int32& ColorID = ColorElement[Key];
					if (ColorID == INDEX_NONE)
					{
						ColorID = Colors->AppendElement(Color);
						HardColorOfElement[Key] = Color;
						Elements.Color = ColorID;
					}
					else
					{
						Elements.Color = HardColorOfElement[Key] == Color ? ColorID : Colors->AppendElement(Color);
					}
				}
			}
		}
		if (NeedsCentre[RegionID])
		{
			FVector CellSum(0.0);
			int32 NumCorners = 0;
			for (int32 Index = 0; Index < N; ++Index)
			{
				if (IsRectCorner(Region, PolyVertex[Base + Index]))
				{
					CellSum += CellsOf(PolyVertex[Base + Index]);
					++NumCorners;
				}
			}
			FCornerElements& Centre = CornerElements[N];
			Centre.Normal = Normals->AppendElement(Normal);
			if (UVs)
			{
				Centre.UV = UVs->AppendElement(MakeUV(CellSum / FMath::Max(NumCorners, 1)));
			}
			if (Colors)
			{
				Centre.Color = Colors->AppendElement(bSmooth ? CornerColor(Region.RepFace, 0) : Input.Color[Point]);
			}
		}

		for (int32 Tri = 0; Tri < TriCount[RegionID]; ++Tri)
		{
			const int32 TID = TriangleID[Base + Tri];
			if (TID < 0)
			{
				continue;
			}
			const FIntVector& C = TriCorner[Base + Tri];
			MaterialIDs->SetValue(TID, Slot);
			Normals->SetTriangle(TID, UE::Geometry::FIndex3i(CornerElements[C.X].Normal, CornerElements[C.Y].Normal, CornerElements[C.Z].Normal));
			if (UVs)
			{
				UVs->SetTriangle(TID, UE::Geometry::FIndex3i(CornerElements[C.X].UV, CornerElements[C.Y].UV, CornerElements[C.Z].UV));
			}
			if (Colors)
			{
				Colors->SetTriangle(TID, UE::Geometry::FIndex3i(CornerElements[C.X].Color, CornerElements[C.Y].Color, CornerElements[C.Z].Color));
			}
		}
		++NumRegionsEmitted;
	}
	EndStage(5);

	// Offsets and non-uniform output scale bend faces off their axis; recompute the normals over the same hard splits.
	UE::Geometry::FMeshNormals::QuickRecomputeOverlayNormals(OutMesh);
	EndStage(6);

	if (OutStats)
	{
		OutStats->NumFaces = NumFaces;
		OutStats->NumRegions = NumRegionsEmitted;
	}
}
