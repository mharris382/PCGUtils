// Copyright Max Harris
//
// The beveled voxel surface. It starts from the same closed quad surface as the plain mesher (one quad per exposed
// cell side, vertices split where cells touch only along an edge or at a corner) and changes it in three ways:
//   - Trim: each quad is pulled back from its beveled edges.
//   - Strip: each beveled edge becomes a sloped band joining the two trimmed quads beside it.
//   - Fill: the hole left round each surface vertex is closed with one polygon.
// Bevel widths are kept per (surface vertex, axis), so a band along a long edge is continuous, and are then scaled
// down wherever two bevels would overlap on one face. Every polygon is wound outward and shares its edges exactly
// with its neighbours, so the result is closed.

#include "Voxel/PCGVoxelMesher.h"

#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelBevelMesherPrivate
{
	using UE::Geometry::FDynamicMesh3;

	// A direction index d: axis d / 2, positive when d is even. Same convention as the plain mesher.
	int32 DirAxis(int32 Dir) { return Dir / 2; }
	int32 DirSign(int32 Dir) { return (Dir % 2 == 0) ? 1 : -1; }
	int32 MakeDir(int32 Axis, int32 Sign) { return Axis * 2 + (Sign > 0 ? 0 : 1); }

	/** The two in-plane axes of a face on Axis, in cyclic order so that U x V = Axis. */
	int32 PlaneU(int32 Axis) { return (Axis + 1) % 3; }
	int32 PlaneV(int32 Axis) { return (Axis + 2) % 3; }

	// Face corners in canonical (u, v) order: counter-clockwise seen from the positive axis. Edge e joins corner e
	// and corner e + 1: edges 0 and 2 run along u, edges 1 and 3 along v.
	constexpr int32 CornerDU[4] = { 0, 1, 1, 0 };
	constexpr int32 CornerDV[4] = { 0, 0, 1, 1 };

	// Corners in the order that runs counter-clockwise seen from outside the solid.
	constexpr int32 OutwardPositive[4] = { 0, 1, 2, 3 };
	constexpr int32 OutwardNegative[4] = { 0, 3, 2, 1 };

	enum EEdgeKind : uint8 { Flat = 0, Convex = 1, Concave = 2 };

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

	/** The grid axis a face edge runs along. */
	int32 EdgeAxis(const FFace& Face, int32 Edge)
	{
		const int32 Axis = DirAxis(Face.Dir);
		return (Edge % 2 == 0) ? PlaneU(Axis) : PlaneV(Axis);
	}

	/** The axis and sign pointing from a face edge into its face. */
	void EdgeInward(const FFace& Face, int32 Edge, int32& OutAxis, int32& OutSign)
	{
		const int32 Axis = DirAxis(Face.Dir);
		switch (Edge)
		{
		case 0:  OutAxis = PlaneV(Axis); OutSign = 1; break;
		case 1:  OutAxis = PlaneU(Axis); OutSign = -1; break;
		case 2:  OutAxis = PlaneV(Axis); OutSign = -1; break;
		default: OutAxis = PlaneU(Axis); OutSign = 1; break;
		}
	}

	/** The canonical edge joining two neighbouring corners. */
	int32 EdgeBetween(int32 A, int32 B) { return ((A + 1) % 4 == B) ? A : B; }

	/** At Corner, the face edge that does not lead to OtherCorner. */
	int32 OtherEdge(int32 Corner, int32 OtherCorner) { return ((Corner + 1) % 4 == OtherCorner) ? (Corner + 3) % 4 : Corner; }

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

	/** Newell's normal of a polygon: its area-weighted normal, pointing the way it winds counter-clockwise. */
	template <typename TGetter>
	FVector Newell(int32 Num, TGetter&& Get)
	{
		FVector Normal(0.0);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector A = Get(Index);
			const FVector B = Get((Index + 1) % Num);
			Normal.X += (A.Y - B.Y) * (A.Z + B.Z);
			Normal.Y += (A.Z - B.Z) * (A.X + B.X);
			Normal.Z += (A.X - B.X) * (A.Y + B.Y);
		}
		return Normal;
	}

	/**
	 * Triangulates a counter-clockwise polygon (possibly a little out of plane) by ear clipping in its Newell plane.
	 * Triangles are local corner indices, counter-clockwise like the polygon. Returns false when no clean ear
	 * clipping exists; the caller then fans round a centre point.
	 */
	bool EarClip(TConstArrayView<FVector> Points, TArray<FIntVector, TInlineAllocator<16>>& OutTris)
	{
		const int32 Num = Points.Num();
		OutTris.Reset();
		if (Num == 3)
		{
			OutTris.Add(FIntVector(0, 1, 2));
			return true;
		}
		FVector Normal = Newell(Num, [&](int32 I) { return Points[I]; });
		if (!Normal.Normalize(1e-12))
		{
			// No area at all: any fan keeps the edges joined.
			for (int32 Index = 1; Index + 1 < Num; ++Index)
			{
				OutTris.Add(FIntVector(0, Index, Index + 1));
			}
			return true;
		}
		const FVector Tangent = FMath::Abs(Normal.X) < 0.9 ? FVector::CrossProduct(Normal, FVector::XAxisVector).GetSafeNormal()
			: FVector::CrossProduct(Normal, FVector::YAxisVector).GetSafeNormal();
		const FVector Bitangent = FVector::CrossProduct(Normal, Tangent);
		TArray<FVector2d, TInlineAllocator<16>> Flat;
		for (const FVector& Point : Points)
		{
			Flat.Add(FVector2d(Point.Dot(Tangent), Point.Dot(Bitangent)));
		}
		// Tangent x Bitangent = Normal, so counter-clockwise polygons have positive area here.
		TArray<int32, TInlineAllocator<16>> Ring;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Ring.Add(Index);
		}
		constexpr double Eps = 1e-9;
		auto Cross = [&](int32 A, int32 B, int32 C)
		{
			const FVector2d AB = Flat[B] - Flat[A];
			const FVector2d BC = Flat[C] - Flat[B];
			return AB.X * BC.Y - AB.Y * BC.X;
		};
		auto Inside = [&](int32 P, int32 A, int32 B, int32 C)
		{
			if (Flat[P].Equals(Flat[A], 1e-12) || Flat[P].Equals(Flat[B], 1e-12) || Flat[P].Equals(Flat[C], 1e-12))
			{
				return false;
			}
			return Cross(A, B, P) > Eps && Cross(B, C, P) > Eps && Cross(C, A, P) > Eps;
		};
		auto IsEar = [&](int32 At)
		{
			const int32 N = Ring.Num();
			const int32 A = Ring[(At + N - 1) % N];
			const int32 B = Ring[At];
			const int32 C = Ring[(At + 1) % N];
			if (Cross(A, B, C) <= Eps)
			{
				return false;
			}
			for (int32 Other = 0; Other < N; ++Other)
			{
				const int32 P = Ring[Other];
				if (P != A && P != B && P != C && Inside(P, A, B, C))
				{
					return false;
				}
			}
			return true;
		};
		auto IsFlatAt = [&](int32 At)
		{
			const int32 N = Ring.Num();
			return FMath::Abs(Cross(Ring[(At + N - 1) % N], Ring[At], Ring[(At + 1) % N])) <= Eps;
		};
		while (Ring.Num() > 3)
		{
			const int32 N = Ring.Num();
			int32 Pick = INDEX_NONE;
			// An ear next to a straight corner first, so straight corners become ears in turn.
			for (int32 At = 0; At < N && Pick == INDEX_NONE; ++At)
			{
				if ((IsFlatAt((At + N - 1) % N) || IsFlatAt((At + 1) % N)) && IsEar(At))
				{
					Pick = At;
				}
			}
			for (int32 At = 0; At < N && Pick == INDEX_NONE; ++At)
			{
				if (IsEar(At))
				{
					Pick = At;
				}
			}
			if (Pick == INDEX_NONE)
			{
				return false;
			}
			OutTris.Add(FIntVector(Ring[(Pick + N - 1) % N], Ring[Pick], Ring[(Pick + 1) % N]));
			Ring.RemoveAt(Pick, EAllowShrinking::No);
		}
		if (Cross(Ring[0], Ring[1], Ring[2]) <= Eps)
		{
			return false;
		}
		OutTris.Add(FIntVector(Ring[0], Ring[1], Ring[2]));
		return true;
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

	enum EPolyKind : uint8 { FacePoly = 0, StripPoly = 1, CornerPoly = 2 };
}

void PCGVoxelMesher::BuildBeveledMesh(const FPCGVoxelMeshInput& Input, const FPCGVoxelMeshOptions& Options,
	UE::Geometry::FDynamicMesh3& OutMesh, FPCGVoxelMeshStats* OutStats)
{
	using namespace PCGVoxelBevelMesherPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGVoxelMesher::BuildBeveledMesh);

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
	const bool bAtlas = Options.UVMode == EPCGVoxelUVMode::AtlasFromAttribute && Input.AtlasRect.Num() == Grid.NumPoints();
	const bool bScale = Input.BevelScale.Num() == Grid.NumPoints();
	auto SlotOf = [&](int32 Point) { return bMaterials ? Input.MaterialSlot[Point] : 0; };
	auto ScaleOf = [&](int32 Point) { return bScale ? FMath::Max(0.0f, Input.BevelScale[Point]) : 1.0f; };

	// ---- 1. Faces: every cell side whose neighbour is empty, stored per cell in direction order.
	const int32 NumCells = Grid.NumCells();
	TArray<uint8> FaceMask;
	FaceMask.SetNumUninitialized(NumCells);
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.FaceMask"), NumCells, 4096, [&](int32 CellIndex)
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
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.Faces"), NumCells, 4096, [&](int32 CellIndex)
	{
		int32 FaceID = FaceBase[CellIndex];
		for (int32 Dir = 0; Dir < 6; ++Dir)
		{
			if (FaceMask[CellIndex] & (1 << Dir))
			{
				Faces[FaceID++] = { Grid.Cells[CellIndex], Dir, Grid.CellPoint[CellIndex] };
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

	// ---- 2. Edges: for each face edge, the partner face across it (as the partner's corner uses meeting this edge's
	// two corners), and whether the surface is flat, convex or concave there. Then vertices, as in the plain mesher.
	TArray<FIntPoint> EdgeJoin;
	EdgeJoin.SetNumUninitialized(NumFaces * 4);
	TArray<uint8> EdgeKind;
	EdgeKind.SetNumUninitialized(NumFaces * 4);
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.EdgeJoin"), NumFaces, 4096, [&](int32 FaceID)
	{
		const FFace& Face = Faces[FaceID];
		const int32 Axis = DirAxis(Face.Dir);
		FIntVector OutOfFace(0);
		OutOfFace[Axis] = DirSign(Face.Dir);
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			const int32 TAxis = (Edge == 0 || Edge == 2) ? PlaneV(Axis) : PlaneU(Axis);
			const int32 TSign = (Edge == 0 || Edge == 3) ? -1 : 1;
			FIntVector Across(0);
			Across[TAxis] = TSign;
			const FIntVector Side = Face.Cell + Across;
			int32 Partner = INDEX_NONE;
			uint8 Kind = Flat;
			const int32 SideCell = Grid.FindCellIndex(Side);
			if (SideCell == INDEX_NONE)
			{
				Partner = FaceAt(Face.Cell, MakeDir(TAxis, TSign));
				Kind = Convex;
			}
			else
			{
				const int32 DiagonalCell = Grid.FindCellIndex(Side + OutOfFace);
				if (DiagonalCell == INDEX_NONE)
				{
					Partner = FaceOfCell(SideCell, Face.Dir);
					Kind = Flat;
				}
				else
				{
					Partner = FaceOfCell(DiagonalCell, MakeDir(TAxis, -TSign));
					Kind = Concave;
				}
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
			EdgeKind[FaceID * 4 + Edge] = Kind;
		}
	});

	const int32 NumUses = NumFaces * 4;
	TArray<int32> VertexOfCorner;
	VertexOfCorner.SetNumUninitialized(NumUses);
	TArray<FIntVector> VertexLattice;
	{
		FUnionFind Corners(NumUses);
		for (int32 FaceEdge = 0; FaceEdge < NumUses; ++FaceEdge)
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
		TArray<int32> RootToVertex;
		RootToVertex.Init(INDEX_NONE, NumUses);
		VertexLattice.Reserve(NumFaces);
		for (int32 Use = 0; Use < NumUses; ++Use)
		{
			const int32 Root = Corners.Find(Use);
			if (RootToVertex[Root] == INDEX_NONE)
			{
				RootToVertex[Root] = VertexLattice.Num();
				VertexLattice.Add(CornerLattice(Faces[Use / 4], Use % 4));
			}
			VertexOfCorner[Use] = RootToVertex[Root];
		}
	}
	const int32 NumVertices = VertexLattice.Num();
	EndStage(1);

	// ---- 3. Bevel widths. Per face edge from its kind, facing and the points' scales; then per (vertex, axis) the
	// narrowest beveled edge along that axis at the vertex, so neighbouring bands meet; then scaled down wherever
	// two bevels would overlap.
	TArray<float> EdgeAmount;
	EdgeAmount.SetNumZeroed(NumUses);
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.EdgeAmount"), NumFaces, 4096, [&](int32 FaceID)
	{
		const FFace& Face = Faces[FaceID];
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			const int32 FaceEdge = FaceID * 4 + Edge;
			const uint8 Kind = EdgeKind[FaceEdge];
			if (Kind == Flat || EdgeJoin[FaceEdge].X == INDEX_NONE)
			{
				continue;
			}
			const FFace& Partner = Faces[EdgeJoin[FaceEdge].X / 4];
			const int32 Facing = (Face.Dir == 4 || Partner.Dir == 4) ? PCGVoxelBevel::Top
				: (Face.Dir == 5 || Partner.Dir == 5) ? PCGVoxelBevel::Bottom : PCGVoxelBevel::Side;
			const float Base = Options.BevelAmount[Kind == Convex ? PCGVoxelBevel::Convex : PCGVoxelBevel::Concave][Facing];
			const float Scale = Kind == Convex ? ScaleOf(Face.Point) : 0.5f * (ScaleOf(Face.Point) + ScaleOf(Partner.Point));
			const float Amount = FMath::Clamp(Base * Scale, 0.0f, 1.0f);
			EdgeAmount[FaceEdge] = Amount > 1e-5f ? Amount : 0.0f;
		}
	});

	TArray<float> VertexAmount;
	VertexAmount.Init(TNumericLimits<float>::Max(), NumVertices * 3);
	for (int32 FaceEdge = 0; FaceEdge < NumUses; ++FaceEdge)
	{
		const float Amount = EdgeAmount[FaceEdge];
		if (Amount <= 0.0f)
		{
			continue;
		}
		const int32 FaceID = FaceEdge / 4;
		const int32 Edge = FaceEdge % 4;
		const int32 Axis = EdgeAxis(Faces[FaceID], Edge);
		for (const int32 Corner : { Edge, (Edge + 1) % 4 })
		{
			float& Slot = VertexAmount[VertexOfCorner[FaceID * 4 + Corner] * 3 + Axis];
			Slot = FMath::Min(Slot, Amount);
		}
	}
	for (float& Amount : VertexAmount)
	{
		if (Amount > 1.0f)
		{
			Amount = 0.0f;
		}
	}
	auto AmountRef = [&](int32 FaceID, int32 Edge, int32 Corner) -> float*
	{
		if (EdgeAmount[FaceID * 4 + Edge] <= 0.0f)
		{
			return nullptr;
		}
		return &VertexAmount[VertexOfCorner[FaceID * 4 + Corner] * 3 + EdgeAxis(Faces[FaceID], Edge)];
	};

	// Scaling only ever shrinks widths, so a constraint met once stays met: one pass is enough.
	for (int32 FaceID = 0; FaceID < NumFaces; ++FaceID)
	{
		// Two opposite beveled edges of one face must not cross: at each end, their widths add up to at most 1.
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			const int32 Opposite = Edge + 2;
			for (const FIntPoint Pair : { FIntPoint(Edge, (Edge + 3) % 4), FIntPoint((Edge + 1) % 4, Edge + 2) })
			{
				float* A = AmountRef(FaceID, Edge, Pair.X);
				float* B = AmountRef(FaceID, Opposite, Pair.Y);
				if (A && B && *A + *B > 1.0f)
				{
					const float Sum = *A + *B;
					*A /= Sum;
					*B /= Sum;
				}
			}
		}
	}
	for (int32 FaceEdge = 0; FaceEdge < NumUses; ++FaceEdge)
	{
		// Along a square (unbeveled) edge, the two faces' trims from each end must not pass each other.
		const FIntPoint Join = EdgeJoin[FaceEdge];
		const int32 FaceID = FaceEdge / 4;
		if (EdgeAmount[FaceEdge] > 0.0f || Join.X == INDEX_NONE || Join.X / 4 < FaceID)
		{
			continue;
		}
		const int32 Edge = FaceEdge % 4;
		const int32 C0 = Edge;
		const int32 C1 = (Edge + 1) % 4;
		const int32 PartnerID = Join.X / 4;
		const int32 G0 = Join.X % 4;
		const int32 G1 = Join.Y % 4;
		float* Refs[4] = {
			AmountRef(FaceID, OtherEdge(C0, C1), C0), AmountRef(PartnerID, OtherEdge(G0, G1), G0),
			AmountRef(FaceID, OtherEdge(C1, C0), C1), AmountRef(PartnerID, OtherEdge(G1, G0), G1) };
		auto Value = [](const float* Ref) { return Ref ? *Ref : 0.0f; };
		const float Sum = FMath::Max(Value(Refs[0]), Value(Refs[1])) + FMath::Max(Value(Refs[2]), Value(Refs[3]));
		if (Sum > 1.0f)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				bool bSeen = Refs[Index] == nullptr;
				for (int32 Before = 0; Before < Index && !bSeen; ++Before)
				{
					bSeen = Refs[Before] == Refs[Index];
				}
				if (!bSeen)
				{
					*Refs[Index] /= Sum;
				}
			}
		}
	}
	EndStage(2);

	// ---- 4. Trimmed corner positions (lattice units), one per face corner, and the output vertices: corners round
	// one surface vertex that land on the same spot share a vertex. Each component gets at most one offset, so equal
	// trims give bit-equal positions.
	TArray<FVector> UsePosition;
	UsePosition.SetNumUninitialized(NumUses);
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.Trim"), NumFaces, 4096, [&](int32 FaceID)
	{
		const FFace& Face = Faces[FaceID];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			FVector Offset(0.0);
			for (const int32 Edge : { Corner, (Corner + 3) % 4 })
			{
				if (const float* Amount = AmountRef(FaceID, Edge, Corner))
				{
					int32 InAxis = 0, InSign = 1;
					EdgeInward(Face, Edge, InAxis, InSign);
					Offset[InAxis] = InSign * static_cast<double>(*Amount);
				}
			}
			UsePosition[FaceID * 4 + Corner] = FVector(VertexLattice[VertexOfCorner[FaceID * 4 + Corner]]) + Offset;
		}
	});

	// Uses bucketed by vertex.
	TArray<int32> VertexUseStart;
	VertexUseStart.Init(0, NumVertices + 1);
	for (int32 Use = 0; Use < NumUses; ++Use)
	{
		++VertexUseStart[VertexOfCorner[Use] + 1];
	}
	for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
	{
		VertexUseStart[Vertex + 1] += VertexUseStart[Vertex];
	}
	TArray<int32> Bucket;
	Bucket.SetNumUninitialized(NumUses);
	{
		TArray<int32> Cursor(VertexUseStart.GetData(), NumVertices);
		for (int32 Use = 0; Use < NumUses; ++Use)
		{
			Bucket[Cursor[VertexOfCorner[Use]]++] = Use;
		}
	}

	// One output vertex per face corner to start with. Corners that land on one spot are merged later only where a
	// polygon edge joins them (the weld below), never just for sharing a spot: where the surface passes along one
	// lattice edge twice (two cells touching along an edge under an overhang), the two passes must stay apart.
	TArray<int32> OutVertexOfUse;
	OutVertexOfUse.SetNumUninitialized(NumUses);
	TArray<FVector> OutPosition = UsePosition;   // lattice units
	TArray<int32> OutOwner;                      // surface vertex of each output vertex
	OutOwner.SetNumUninitialized(NumUses);
	for (int32 Use = 0; Use < NumUses; ++Use)
	{
		OutVertexOfUse[Use] = Use;
		OutOwner[Use] = VertexOfCorner[Use];
	}
	EndStage(3);

	// ---- 5. Polygons, each counter-clockwise seen from outside.
	TArray<int32> PolyStart;
	TArray<int32> PolyVertex;
	TArray<uint8> PolyKind;
	TArray<int32> PolyFace;            // the face whose point gives material and color; for faces, the face itself
	PolyStart.Reserve(NumFaces * 2 + 1);
	PolyVertex.Reserve(NumFaces * 6);
	PolyStart.Add(0);
	TArray<int32, TInlineAllocator<16>> Ring;
	using FLoop = TArray<int32, TInlineAllocator<16>>;
	auto Commit = [&](uint8 Kind, int32 FaceID)
	{
		// Drop repeats (also across the wrap). A loop that visits one vertex twice is two loops touching there (round a
		// saddle vertex the hole is a figure of eight), so it is split there. Each loop needs three distinct corners.
		TArray<FLoop, TInlineAllocator<2>> Pending;
		Pending.Emplace(Ring);
		Ring.Reset();
		while (!Pending.IsEmpty())
		{
			const FLoop Loop = Pending.Pop(EAllowShrinking::No);
			FLoop Clean;
			for (const int32 Vertex : Loop)
			{
				if (Clean.IsEmpty() || Clean.Last() != Vertex)
				{
					Clean.Add(Vertex);
				}
			}
			while (Clean.Num() > 1 && Clean.Last() == Clean[0])
			{
				Clean.Pop(EAllowShrinking::No);
			}
			if (Clean.Num() < 3)
			{
				continue;
			}
			int32 First = INDEX_NONE, Second = INDEX_NONE;
			for (int32 I = 0; I < Clean.Num() && First == INDEX_NONE; ++I)
			{
				for (int32 J = I + 1; J < Clean.Num(); ++J)
				{
					if (Clean[I] == Clean[J])
					{
						First = I;
						Second = J;
						break;
					}
				}
			}
			if (First != INDEX_NONE)
			{
				FLoop& A = Pending.Emplace_GetRef();
				for (int32 I = First; I < Second; ++I)
				{
					A.Add(Clean[I]);
				}
				FLoop& B = Pending.Emplace_GetRef();
				for (int32 I = Second; I < Clean.Num(); ++I)
				{
					B.Add(Clean[I]);
				}
				for (int32 I = 0; I < First; ++I)
				{
					B.Add(Clean[I]);
				}
				continue;
			}
			PolyVertex.Append(Clean);
			PolyStart.Add(PolyVertex.Num());
			PolyKind.Add(Kind);
			PolyFace.Add(FaceID);
		}
	};
	auto OutwardOrder = [&](int32 FaceID) { return DirSign(Faces[FaceID].Dir) > 0 ? OutwardPositive : OutwardNegative; };

	// 5a. Faces. Along a square edge, the neighbour's trimmed corners that fall inside this face's edge are added, so
	// both sides share every vertex on that line. Kept as rings (INDEX_NONE marks a removed corner) and committed after
	// the corners, which may cut a face's corner away.
	TArray<int32> FaceRingStart;
	TArray<int32> FaceRingVertex;
	TArray<FIntPoint> WeldPairs;       // corners of two faces that meet at one spot on a square edge
	FaceRingStart.SetNumUninitialized(NumFaces + 1);
	FaceRingVertex.Reserve(NumFaces * 5);
	for (int32 FaceID = 0; FaceID < NumFaces; ++FaceID)
	{
		FaceRingStart[FaceID] = FaceRingVertex.Num();
		const int32* Order = OutwardOrder(FaceID);
		for (int32 K = 0; K < 4; ++K)
		{
			const int32 A = Order[K];
			const int32 B = Order[(K + 1) % 4];
			const int32 Edge = EdgeBetween(A, B);
			Ring.Add(OutVertexOfUse[FaceID * 4 + A]);
			const FIntPoint Join = EdgeJoin[FaceID * 4 + Edge];
			if (EdgeAmount[FaceID * 4 + Edge] > 0.0f || Join.X == INDEX_NONE)
			{
				continue;
			}
			const int32 PartnerA = (A == Edge) ? Join.X : Join.Y;
			const int32 PartnerB = (A == Edge) ? Join.Y : Join.X;
			const FVector Start(VertexLattice[VertexOfCorner[FaceID * 4 + A]]);
			const FVector Along = FVector(VertexLattice[VertexOfCorner[FaceID * 4 + B]]) - Start;
			const double TA = (UsePosition[FaceID * 4 + A] - Start).Dot(Along);
			const double TB = (UsePosition[FaceID * 4 + B] - Start).Dot(Along);
			double T0 = (UsePosition[PartnerA] - Start).Dot(Along);
			double T1 = (UsePosition[PartnerB] - Start).Dot(Along);
			int32 V0 = OutVertexOfUse[PartnerA];
			int32 V1 = OutVertexOfUse[PartnerB];
			if (T1 < T0)
			{
				Swap(T0, T1);
				Swap(V0, V1);
			}
			constexpr double Eps = 1e-7;
			for (const TPair<double, int32> Partner : { TPair<double, int32>(T0, V0), TPair<double, int32>(T1, V1) })
			{
				if (FMath::Abs(Partner.Key - TA) <= Eps)
				{
					WeldPairs.Add(FIntPoint(Partner.Value, OutVertexOfUse[FaceID * 4 + A]));
				}
				if (FMath::Abs(Partner.Key - TB) <= Eps)
				{
					WeldPairs.Add(FIntPoint(Partner.Value, OutVertexOfUse[FaceID * 4 + B]));
				}
			}
			if (T0 > TA + Eps && T0 < TB - Eps)
			{
				Ring.Add(V0);
			}
			if (T1 > TA + Eps && T1 < TB - Eps)
			{
				Ring.Add(V1);
			}
		}
		FaceRingVertex.Append(Ring);
		Ring.Reset();
	}
	FaceRingStart[NumFaces] = FaceRingVertex.Num();

	// 5b. Strips, once per beveled edge: this face's trimmed edge, then the partner's, wound against both.
	for (int32 FaceEdge = 0; FaceEdge < NumUses; ++FaceEdge)
	{
		const FIntPoint Join = EdgeJoin[FaceEdge];
		const int32 FaceID = FaceEdge / 4;
		if (EdgeAmount[FaceEdge] <= 0.0f || Join.X == INDEX_NONE || Join.X / 4 < FaceID)
		{
			continue;
		}
		const int32 Edge = FaceEdge % 4;
		const bool bForward = DirSign(Faces[FaceID].Dir) > 0;
		const int32 A = bForward ? Edge : (Edge + 1) % 4;
		const int32 B = bForward ? (Edge + 1) % 4 : Edge;
		const int32 PartnerA = (A == Edge) ? Join.X : Join.Y;
		const int32 PartnerB = (A == Edge) ? Join.Y : Join.X;
		Ring.Add(OutVertexOfUse[FaceID * 4 + B]);
		Ring.Add(OutVertexOfUse[FaceID * 4 + A]);
		Ring.Add(OutVertexOfUse[PartnerA]);
		Ring.Add(OutVertexOfUse[PartnerB]);
		Commit(StripPoly, FaceID);
	}

	// 5c. Corners: walk the faces round each surface vertex counter-clockwise (each next face is across the current
	// face's edge arriving at the vertex) and join their trimmed corners.
	int32 NumOpenFans = 0;
	TArray<int32, TInlineAllocator<16>> RingFace;
	for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
	{
		const int32 Begin = VertexUseStart[Vertex];
		const int32 Count = VertexUseStart[Vertex + 1] - Begin;
		if (Count < 3)
		{
			continue;
		}
		const int32 FirstUse = Bucket[Begin];
		int32 Use = FirstUse;
		int32 Steps = 0;
		RingFace.Reset();
		do
		{
			Ring.Add(OutVertexOfUse[Use]);
			RingFace.Add(Use / 4);
			const int32 FaceID = Use / 4;
			const int32 Corner = Use % 4;
			const int32* Order = OutwardOrder(FaceID);
			int32 K = 0;
			while (Order[K] != Corner)
			{
				++K;
			}
			const int32 Previous = Order[(K + 3) % 4];
			const int32 Edge = EdgeBetween(Previous, Corner);
			const FIntPoint Join = EdgeJoin[FaceID * 4 + Edge];
			Use = (Corner == Edge) ? Join.X : Join.Y;
		}
		while (Use != INDEX_NONE && Use != FirstUse && ++Steps < 64);
		if (Use != FirstUse)
		{
			++NumOpenFans;
			Ring.Reset();
			continue;
		}
		// A face whose two edges here are square, while a third edge here is beveled, keeps its square corner, but its
		// neighbours on both sides are trimmed back along its edges: the hole would fold back over the face. Cut that
		// corner off the face and out of the hole, so the face's new diagonal meets the hole (or the strip end) directly.
		for (bool bCut = true; bCut; )
		{
			bCut = false;
			const int32 N = Ring.Num();
			for (int32 K = 0; K < N && !bCut; ++K)
			{
				const int32 X = Ring[K];
				const int32 Prev = Ring[(K + N - 1) % N];
				const int32 Next = Ring[(K + 1) % N];
				int32 Times = 0;
				for (const int32 Other : Ring)
				{
					Times += Other == X ? 1 : 0;
				}
				if (Times != 1 || Prev == Next)
				{
					continue;
				}
				const int32 FaceID = RingFace[K];
				const int32 RingBegin = FaceRingStart[FaceID];
				const int32 Length = FaceRingStart[FaceID + 1] - RingBegin;
				for (int32 I = 0; I < Length; ++I)
				{
					if (FaceRingVertex[RingBegin + I] != X)
					{
						continue;
					}
					int32 Before = INDEX_NONE, After = INDEX_NONE;
					for (int32 Step = 1; Step < Length && Before == INDEX_NONE; ++Step)
					{
						Before = FaceRingVertex[RingBegin + (I - Step + Length) % Length];
					}
					for (int32 Step = 1; Step < Length && After == INDEX_NONE; ++Step)
					{
						After = FaceRingVertex[RingBegin + (I + Step) % Length];
					}
					// Only a fold when the rest of the hole lies on or behind the face: where the neighbours are trimmed
					// by bevels on opposite sides of the face (under an overhang), the corner stays.
					bool bFold = Before == Next && After == Prev;
					const int32 NormalAxis = DirAxis(Faces[FaceID].Dir);
					const double NormalSign = DirSign(Faces[FaceID].Dir);
					for (int32 Other = 0; Other < N && bFold; ++Other)
					{
						bFold = (OutPosition[Ring[Other]][NormalAxis] - OutPosition[X][NormalAxis]) * NormalSign <= 1e-6;
					}
					if (bFold)
					{
						FaceRingVertex[RingBegin + I] = INDEX_NONE;
						Ring.RemoveAt(K);
						RingFace.RemoveAt(K);
						bCut = true;
					}
					break;
				}
			}
		}
		Commit(CornerPoly, FirstUse / 4);
	}

	for (int32 FaceID = 0; FaceID < NumFaces; ++FaceID)
	{
		for (int32 Slot = FaceRingStart[FaceID]; Slot < FaceRingStart[FaceID + 1]; ++Slot)
		{
			if (FaceRingVertex[Slot] != INDEX_NONE)
			{
				Ring.Add(FaceRingVertex[Slot]);
			}
		}
		Commit(FacePoly, FaceID);
	}

	// 5d. Weld. Where bevels meet exactly (a face trimmed to a line or a point), corners of different surface vertices
	// land on one spot, joined by zero-length polygon edges. Merge them and drop the polygons that collapse.
	{
		FUnionFind Weld(OutPosition.Num());
		bool bAnyWeld = false;
		for (const FIntPoint& Pair : WeldPairs)
		{
			if (Pair.X != Pair.Y)
			{
				Weld.Union(Pair.X, Pair.Y);
				bAnyWeld = true;
			}
		}
		for (int32 Poly = 0; Poly < PolyKind.Num(); ++Poly)
		{
			const int32 Base = PolyStart[Poly];
			const int32 N = PolyStart[Poly + 1] - Base;
			for (int32 Index = 0; Index < N; ++Index)
			{
				const int32 A = PolyVertex[Base + Index];
				const int32 B = PolyVertex[Base + (Index + 1) % N];
				if (A != B && OutPosition[A].Equals(OutPosition[B], 1e-6))
				{
					Weld.Union(A, B);
					bAnyWeld = true;
				}
			}
		}
		if (bAnyWeld)
		{
			const TArray<int32> OldStart = MoveTemp(PolyStart);
			const TArray<int32> OldVertex = MoveTemp(PolyVertex);
			const TArray<uint8> OldKind = MoveTemp(PolyKind);
			const TArray<int32> OldFace = MoveTemp(PolyFace);
			PolyStart.Reset();
			PolyVertex.Reset();
			PolyKind.Reset();
			PolyFace.Reset();
			PolyStart.Add(0);
			for (int32 Poly = 0; Poly < OldKind.Num(); ++Poly)
			{
				for (int32 Slot = OldStart[Poly]; Slot < OldStart[Poly + 1]; ++Slot)
				{
					Ring.Add(Weld.Find(OldVertex[Slot]));
				}
				Commit(OldKind[Poly], OldFace[Poly]);
			}
		}
	}
	const int32 NumPolys = PolyKind.Num();
	EndStage(4);

	// ---- 6. Emit.
	const FTransform& GridToWorld = Grid.GridToWorld;
	auto GridOf = [&](const FVector& Lattice) { return (Lattice - FVector(0.5)) * Grid.VoxelSize; };
	auto ToOutput = [&](const FVector& Lattice)
	{
		return Options.OutputSpace.InverseTransformPosition(GridToWorld.TransformPositionNoScale(GridOf(Lattice)));
	};
	const bool bMirrored = Options.OutputSpace.GetDeterminant() < 0.0;
	const bool bFlip = (!MathCrossIsOutward()) != bMirrored;

	// 6a. Triangulate (parallel). Triangles of polygon P live in slots PolyStart[P] .. + TriCount[P]; a polygon of N
	// corners needs at most N slots (N - 2, or N for a centre fan). Corner index N means the polygon's centre.
	TArray<FIntVector> TriCorner;
	TriCorner.SetNumUninitialized(PolyVertex.Num());
	TArray<int32> TriCount;
	TriCount.SetNumZeroed(NumPolys);
	TArray<bool> NeedsCentre;
	NeedsCentre.Init(false, NumPolys);
	ParallelForTemplate(TEXT("PCGVoxelBevelMesher.Triangulate"), NumPolys, 1024, [&](int32 Poly)
	{
		const int32 Base = PolyStart[Poly];
		const int32 N = PolyStart[Poly + 1] - Base;
		TArray<FVector, TInlineAllocator<16>> Points;
		for (int32 Index = 0; Index < N; ++Index)
		{
			Points.Add(OutPosition[PolyVertex[Base + Index]] * Grid.VoxelSize);
		}
		TArray<FIntVector, TInlineAllocator<16>> Tris;
		if (!EarClip(Points, Tris))
		{
			Tris.Reset();
			for (int32 Index = 0; Index < N; ++Index)
			{
				Tris.Add(FIntVector(N, Index, (Index + 1) % N));
			}
			NeedsCentre[Poly] = true;
		}
		for (int32 Tri = 0; Tri < Tris.Num(); ++Tri)
		{
			const FIntVector& T = Tris[Tri];
			TriCorner[Base + Tri] = bFlip ? FIntVector(T.X, T.Z, T.Y) : T;
		}
		TriCount[Poly] = Tris.Num();
	});

	// 6b. Bare mesh.
	OutMesh.EnableTriangleGroups();
	TArray<int32> MeshVertex;
	MeshVertex.Init(INDEX_NONE, OutPosition.Num());
	for (const int32 Out : PolyVertex)
	{
		if (MeshVertex[Out] == INDEX_NONE)
		{
			MeshVertex[Out] = OutMesh.AppendVertex(FVector3d(ToOutput(OutPosition[Out])));
		}
	}
	TMap<int32, int32> CentreVertex;
	TMap<int32, FVector> CentreLattice;
	auto AddCentre = [&](int32 Poly)
	{
		FVector Sum(0.0);
		const int32 Base = PolyStart[Poly];
		const int32 N = PolyStart[Poly + 1] - Base;
		for (int32 Index = 0; Index < N; ++Index)
		{
			Sum += OutPosition[PolyVertex[Base + Index]];
		}
		CentreLattice.Add(Poly, Sum / N);
		CentreVertex.Add(Poly, OutMesh.AppendVertex(FVector3d(ToOutput(Sum / N))));
	};
	for (int32 Poly = 0; Poly < NumPolys; ++Poly)
	{
		if (NeedsCentre[Poly])
		{
			AddCentre(Poly);
		}
	}

	auto GroupOf = [&](int32 Poly) -> int32
	{
		switch (Options.Polygroups)
		{
		case EPCGVoxelPolygroupMode::PerFaceDirection:
			return PolyKind[Poly] == FacePoly ? Faces[PolyFace[Poly]].Dir : (PolyKind[Poly] == StripPoly ? 6 : 7);
		case EPCGVoxelPolygroupMode::PerMaterial:        return SlotOf(Faces[PolyFace[Poly]].Point);
		case EPCGVoxelPolygroupMode::PerCollapsedRegion: return Poly;
		case EPCGVoxelPolygroupMode::PerAttributeValue:
			return Input.Polygroup.IsEmpty() ? 0 : Input.Polygroup[Faces[PolyFace[Poly]].Point];
		default: return 0;
		}
	};
	TArray<int32> TriangleID;
	TriangleID.SetNumUninitialized(PolyVertex.Num());
	int32 NumRejected = 0;
	for (int32 Poly = 0; Poly < NumPolys; ++Poly)
	{
		const int32 Base = PolyStart[Poly];
		const int32 N = PolyStart[Poly + 1] - Base;
		const int32 Group = GroupOf(Poly);
		auto MeshVertexOf = [&](int32 Corner) { return Corner == N ? CentreVertex.FindChecked(Poly) : MeshVertex[PolyVertex[Base + Corner]]; };
		for (int32 Tri = 0; Tri < N; ++Tri)
		{
			TriangleID[Base + Tri] = INDEX_NONE;
		}
		bool bRejected = false;
		for (int32 Tri = 0; Tri < TriCount[Poly] && !bRejected; ++Tri)
		{
			const FIntVector& C = TriCorner[Base + Tri];
			const int32 TID = OutMesh.AppendTriangle(MeshVertexOf(C.X), MeshVertexOf(C.Y), MeshVertexOf(C.Z), Group);
			TriangleID[Base + Tri] = TID;
			bRejected = TID < 0;
		}
		if (bRejected && !NeedsCentre[Poly])
		{
			// A diagonal this polygon chose is already an edge of a neighbour (where bevels meet exactly, two corner
			// fills can share two vertices). Fan round a centre instead: a fan adds no diagonals.
			for (int32 Tri = 0; Tri < TriCount[Poly]; ++Tri)
			{
				if (TriangleID[Base + Tri] >= 0)
				{
					OutMesh.RemoveTriangle(TriangleID[Base + Tri], false, false);
				}
			}
			NeedsCentre[Poly] = true;
			AddCentre(Poly);
			for (int32 Index = 0; Index < N; ++Index)
			{
				TriCorner[Base + Index] = bFlip ? FIntVector(N, (Index + 1) % N, Index) : FIntVector(N, Index, (Index + 1) % N);
			}
			TriCount[Poly] = N;
			bRejected = false;
			for (int32 Tri = 0; Tri < N; ++Tri)
			{
				const FIntVector& C = TriCorner[Base + Tri];
				TriangleID[Base + Tri] = OutMesh.AppendTriangle(MeshVertexOf(C.X), MeshVertexOf(C.Y), MeshVertexOf(C.Z), Group);
				NumRejected += TriangleID[Base + Tri] < 0 ? 1 : 0;
			}
		}
		else if (bRejected)
		{
			for (int32 Tri = 0; Tri < TriCount[Poly]; ++Tri)
			{
				if (TriangleID[Base + Tri] < 0)
				{
					const FIntVector& C = TriCorner[Base + Tri];
					TriangleID[Base + Tri] = OutMesh.AppendTriangle(MeshVertexOf(C.X), MeshVertexOf(C.Y), MeshVertexOf(C.Z), Group);
					NumRejected += TriangleID[Base + Tri] < 0 ? 1 : 0;
				}
			}
		}
	}

	// 6c. Attributes: flat normals and UVs per polygon, materials, colors.
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

	const bool bSmooth = bColors && Options.ColorTransition != EPCGVoxelColorTransition::Hard;
	const bool bByMaterial = Options.ColorTransition == EPCGVoxelColorTransition::SmoothWithinMaterial;
	/** Average color of the distinct cells meeting at a surface vertex (only those of Slot when bByMaterial). */
	auto SmoothColor = [&](int32 Vertex, int32 Slot, const FVector4f& Fallback) -> FVector4f
	{
		TArray<FIntVector, TInlineAllocator<16>> Seen;
		FVector4f Sum(0.0f, 0.0f, 0.0f, 0.0f);
		int32 Num = 0;
		for (int32 Index = VertexUseStart[Vertex]; Index < VertexUseStart[Vertex + 1]; ++Index)
		{
			const FFace& Face = Faces[Bucket[Index] / 4];
			if (Seen.Contains(Face.Cell) || (bByMaterial && SlotOf(Face.Point) != Slot))
			{
				continue;
			}
			Seen.Add(Face.Cell);
			Sum += Input.Color[Face.Point];
			++Num;
		}
		return Num > 0 ? Sum / static_cast<float>(Num) : Fallback;
	};

	struct FCornerElements { int32 Normal = INDEX_NONE; int32 UV = INDEX_NONE; int32 Color = INDEX_NONE; };
	TArray<FCornerElements, TInlineAllocator<17>> CornerElements;
	TArray<FVector, TInlineAllocator<17>> CornerLatticePos;
	for (int32 Poly = 0; Poly < NumPolys; ++Poly)
	{
		const int32 Base = PolyStart[Poly];
		const int32 N = PolyStart[Poly + 1] - Base;
		const FFace& SourceFace = Faces[PolyFace[Poly]];
		const int32 Point = SourceFace.Point;
		const int32 Slot = SlotOf(Point);

		CornerLatticePos.Reset();
		for (int32 Index = 0; Index < N; ++Index)
		{
			CornerLatticePos.Add(OutPosition[PolyVertex[Base + Index]]);
		}
		if (NeedsCentre[Poly])
		{
			CornerLatticePos.Add(CentreLattice.FindChecked(Poly));
		}

		// Normal: from the output positions, so offsets of the output transform are followed exactly.
		FVector Normal = Newell(N, [&](int32 I) { return ToOutput(CornerLatticePos[I]); }) * (bMirrored ? -1.0 : 1.0);
		const FVector GridNormal = Newell(N, [&](int32 I) { return CornerLatticePos[I] * Grid.VoxelSize; });
		int32 Dir = SourceFace.Dir;
		if (PolyKind[Poly] != FacePoly)
		{
			const FVector Abs = GridNormal.GetAbs();
			const int32 Axis = (Abs.X >= Abs.Y && Abs.X >= Abs.Z) ? 0 : (Abs.Y >= Abs.Z ? 1 : 2);
			Dir = MakeDir(Axis, GridNormal[Axis] >= 0.0 ? 1 : -1);
		}
		if (!Normal.Normalize(1e-12))
		{
			FVector Axis(0.0);
			Axis[DirAxis(Dir)] = DirSign(Dir);
			Normal = Options.OutputSpace.InverseTransformVector(GridToWorld.TransformVectorNoScale(Axis)).GetSafeNormal();
		}
		const int32 NormalID = Normals->AppendElement(FVector3f(Normal));

		FVector2d UVOrigin(TNumericLimits<double>::Max());
		if (UVs && Options.UVMode != EPCGVoxelUVMode::GridProjection)
		{
			for (int32 Index = 0; Index < N; ++Index)
			{
				const FVector2d Projected = Project(CornerLatticePos[Index] - FVector(0.5), Dir);
				UVOrigin = FVector2d(FMath::Min(UVOrigin.X, Projected.X), FMath::Min(UVOrigin.Y, Projected.Y));
			}
		}
		auto MakeUV = [&](const FVector& Lattice) -> FVector2f
		{
			const FVector2d Projected = Project(Lattice - FVector(0.5), Dir);
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

		const int32 NumCorners = CornerLatticePos.Num();
		CornerElements.SetNum(NumCorners, EAllowShrinking::No);
		for (int32 Index = 0; Index < NumCorners; ++Index)
		{
			FCornerElements& Elements = CornerElements[Index];
			Elements.Normal = NormalID;
			if (UVs)
			{
				Elements.UV = UVs->AppendElement(MakeUV(CornerLatticePos[Index]));
			}
			if (Colors)
			{
				const FVector4f& Own = Input.Color[Point];
				const int32 Owner = Index < N ? OutOwner[PolyVertex[Base + Index]] : INDEX_NONE;
				Elements.Color = Colors->AppendElement(bSmooth && Owner != INDEX_NONE ? SmoothColor(Owner, Slot, Own) : Own);
			}
		}

		for (int32 Tri = 0; Tri < TriCount[Poly]; ++Tri)
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
	}
	EndStage(5);

	if (OutStats)
	{
		OutStats->NumFaces = NumFaces;
		OutStats->NumRegions = NumPolys;
		OutStats->NumRejectedTriangles = NumRejected;
		OutStats->NumOpenFans = NumOpenFans;
	}
}
