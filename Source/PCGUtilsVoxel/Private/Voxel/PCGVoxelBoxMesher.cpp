// Copyright Max Harris
//
// Exact offsets: the surface of the union of every point's own box (a cell or chunk grown or pulled in by its
// offsets), meshed directly. Box faces are cut against the boxes in front of them, so each exposed piece appears once;
// pieces are welded, every edge is split at the vertices lying on it (no T-junctions), the four faces at a pinch edge
// are paired, and polygons are triangulated without zero-area triangles. Coordinates are whole steps of 1/2^20 cell,
// so every comparison is exact.

#include "Voxel/PCGVoxelMesher.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Algo/BinarySearch.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Voxel/PCGVoxelGrid.h"

namespace PCGVoxelBoxMesherPrivate
{
	using UE::Geometry::FDynamicMesh3;

	constexpr int64 Steps = int64(1) << 20; // quantization steps per cell

	int32 PlaneU(int32 Axis) { return (Axis + 1) % 3; }
	int32 PlaneV(int32 Axis) { return (Axis + 2) % 3; }
	int32 MakeDir(int32 Axis, int32 Sign) { return Axis * 2 + (Sign > 0 ? 0 : 1); }

	int64 FloorDiv(int64 A, int64 B) { return A >= 0 ? A / B : -((-A + B - 1) / B); }

	struct FQBox
	{
		int64 Lo[3];
		int64 Hi[3];
		int32 Point = INDEX_NONE;
	};

	/** An exposed face piece: an axis-aligned rectangle on Plane, facing Sign along Axis. */
	struct FRect
	{
		int32 Axis = 0;
		int32 Sign = 1;
		int64 Plane = 0;
		int64 U0 = 0, U1 = 0, V0 = 0, V1 = 0;
		int32 Point = INDEX_NONE;
		int32 Class = 0; // faces of one class may be joined when collapsing
		int32 Dir() const { return MakeDir(Axis, Sign); }
	};

	struct FUnionFind
	{
		TArray<int32> Parent;
		int32 Add() { return Parent.Add(Parent.Num()); }
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

	/** Which way is outward for the engine (see PCGVoxelMesher.cpp). */
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

	/** Face (u, v) projected to texture (U, V) so side faces stay upright. Matches PCGVoxelMesher.cpp. */
	FVector2d Project(const FVector& Cells, int32 Dir)
	{
		switch (Dir)
		{
		case 0: return FVector2d(Cells.Y, -Cells.Z);
		case 1: return FVector2d(-Cells.Y, -Cells.Z);
		case 2: return FVector2d(-Cells.X, -Cells.Z);
		case 3: return FVector2d(Cells.X, -Cells.Z);
		case 4: return FVector2d(Cells.X, -Cells.Y);
		default: return FVector2d(Cells.X, Cells.Y);
		}
	}

	/** Joins neighbouring rectangles of one plane and class along U (equal V span), then along V (equal U span). */
	void JoinRects(TArray<FRect>& Rects)
	{
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bAlongU = Pass == 0;
			Rects.Sort([bAlongU](const FRect& A, const FRect& B)
			{
				if (A.Axis != B.Axis) return A.Axis < B.Axis;
				if (A.Sign != B.Sign) return A.Sign < B.Sign;
				if (A.Plane != B.Plane) return A.Plane < B.Plane;
				if (A.Class != B.Class) return A.Class < B.Class;
				if (bAlongU)
				{
					if (A.V0 != B.V0) return A.V0 < B.V0;
					if (A.V1 != B.V1) return A.V1 < B.V1;
					return A.U0 < B.U0;
				}
				if (A.U0 != B.U0) return A.U0 < B.U0;
				if (A.U1 != B.U1) return A.U1 < B.U1;
				return A.V0 < B.V0;
			});
			TArray<FRect> Joined;
			Joined.Reserve(Rects.Num());
			for (const FRect& R : Rects)
			{
				if (!Joined.IsEmpty())
				{
					FRect& L = Joined.Last();
					const bool bSameGroup = L.Axis == R.Axis && L.Sign == R.Sign && L.Plane == R.Plane && L.Class == R.Class;
					if (bSameGroup && bAlongU && L.V0 == R.V0 && L.V1 == R.V1 && L.U1 == R.U0)
					{
						L.U1 = R.U1;
						continue;
					}
					if (bSameGroup && !bAlongU && L.U0 == R.U0 && L.U1 == R.U1 && L.V1 == R.V0)
					{
						L.V1 = R.V1;
						continue;
					}
				}
				Joined.Add(R);
			}
			Rects = MoveTemp(Joined);
		}
	}
}

bool PCGVoxelMesher::BuildExactBoxMesh(const FPCGVoxelMeshInput& Input, const FPCGVoxelMeshOptions& Options,
	UE::Geometry::FDynamicMesh3& OutMesh, FPCGVoxelMeshStats* OutStats)
{
	using namespace PCGVoxelBoxMesherPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGVoxelMesher::BuildExactBoxMesh);

	const FPCGVoxelGrid& Grid = *Input.Grid;
	const int32 NumPoints = Grid.NumPoints();
	const bool bColors = Input.Color.Num() == NumPoints;
	const bool bMaterials = Input.MaterialSlot.Num() == NumPoints;
	const bool bDropFaces = Input.DroppedFaces.Num() == NumPoints;
	const bool bAtlas = Options.UVMode == EPCGVoxelUVMode::AtlasFromAttribute && Input.AtlasRect.Num() == NumPoints;
	const bool bCollapse = Options.bCollapseCoplanarFaces && Options.UVMode != EPCGVoxelUVMode::AtlasFromAttribute;
	const bool bSmooth = bColors && Options.ColorTransition != EPCGVoxelColorTransition::Hard;
	const bool bByMaterial = Options.ColorTransition == EPCGVoxelColorTransition::SmoothWithinMaterial;
	auto SlotOf = [&](int32 Point) { return bMaterials ? Input.MaterialSlot[Point] : 0; };

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

	// ---- 1. Boxes: every used point's own box, and every point that shares its owner's cell and span (Offset Surface
	// writes a cell holding several boxes that way). A box with no thickness on an axis cannot be meshed this way.
	TArray<FQBox> Boxes;
	Boxes.Reserve(NumPoints);
	auto ToSteps = [](double Cells) { return static_cast<int64>(FMath::RoundToDouble(Cells * static_cast<double>(Steps))); };
	for (int32 Point = 0; Point < NumPoints; ++Point)
	{
		if (!Grid.PointUsed[Point])
		{
			const int32 Owner = Grid.FindPoint(Grid.PointFirst[Point]);
			if (Owner == INDEX_NONE || Grid.PointFirst[Owner] != Grid.PointFirst[Point] || Grid.PointSpan[Owner] != Grid.PointSpan[Point])
			{
				continue;
			}
		}
		FQBox Box;
		Box.Point = Point;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Size = Grid.VoxelSize[Axis];
			Box.Lo[Axis] = ToSteps(Grid.PointFirst[Point][Axis] - Input.OffsetMin[Point][Axis] / Size);
			Box.Hi[Axis] = ToSteps(Grid.PointFirst[Point][Axis] + Grid.PointSpan[Point][Axis] + Input.OffsetMax[Point][Axis] / Size);
			if (Box.Hi[Axis] <= Box.Lo[Axis])
			{
				return false; // a flat or inverted box: the caller meshes the set the cell way
			}
		}
		Boxes.Add(Box);
	}

	// Which boxes reach into each cell.
	TMap<FIntVector, TArray<int32, TInlineAllocator<2>>> CellBoxes;
	CellBoxes.Reserve(Boxes.Num());
	for (int32 BoxIndex = 0; BoxIndex < Boxes.Num(); ++BoxIndex)
	{
		const FQBox& Box = Boxes[BoxIndex];
		FIntVector Lo, Hi;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Lo[Axis] = static_cast<int32>(FloorDiv(Box.Lo[Axis], Steps));
			Hi[Axis] = static_cast<int32>(FloorDiv(Box.Hi[Axis] - 1, Steps));
		}
		for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					CellBoxes.FindOrAdd(FIntVector(X, Y, Z)).Add(BoxIndex);
				}
	}
	auto BoxesIn = [&](const FIntVector& Cell) -> TConstArrayView<int32>
	{
		const TArray<int32, TInlineAllocator<2>>* Found = CellBoxes.Find(Cell);
		return Found ? TConstArrayView<int32>(*Found) : TConstArrayView<int32>();
	};

	// Faces of one class may be joined when collapsing: same material, color and polygroup value.
	TArray<int32> ClassOf;
	ClassOf.Init(0, NumPoints);
	{
		TMap<FString, int32> Classes;
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			FString Key = FString::Printf(TEXT("%d"), SlotOf(Point));
			if (bColors)
			{
				Key += FString::Printf(TEXT("|%g,%g,%g,%g"), Input.Color[Point].X, Input.Color[Point].Y, Input.Color[Point].Z, Input.Color[Point].W);
			}
			if (Options.Polygroups == EPCGVoxelPolygroupMode::PerAttributeValue && Input.Polygroup.Num() == NumPoints)
			{
				Key += FString::Printf(TEXT("|g%d"), Input.Polygroup[Point]);
			}
			ClassOf[Point] = Classes.FindOrAdd(Key, Classes.Num());
		}
	}
	EndStage(0);

	// ---- 2. Exposed pieces: each box face, cell by cell, less what the boxes in front of it cover and less what an
	// earlier box's face on the same plane already gives.
	TArray<FRect> Rects;
	{
		TArray<int64> UCuts, VCuts;
		TArray<uint8> Open;
		struct FCut { int64 U0, U1, V0, V1; };
		TArray<FCut> Removed;
		for (int32 BoxIndex = 0; BoxIndex < Boxes.Num(); ++BoxIndex)
		{
			const FQBox& Box = Boxes[BoxIndex];
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 UA = PlaneU(Axis);
				const int32 VA = PlaneV(Axis);
				for (const int32 Sign : { 1, -1 })
				{
					if (bDropFaces && (Input.DroppedFaces[Box.Point] & (1 << MakeDir(Axis, Sign))))
					{
						continue;
					}
					const int64 Plane = Sign > 0 ? Box.Hi[Axis] : Box.Lo[Axis];
					// The cell just in front of the face and the one just behind it, along Axis.
					const int32 FrontCell = static_cast<int32>(Sign > 0 ? FloorDiv(Plane, Steps) : FloorDiv(Plane - 1, Steps));
					const int32 BackCell = static_cast<int32>(Sign > 0 ? FloorDiv(Plane - 1, Steps) : FloorDiv(Plane, Steps));
					const int32 CU0 = static_cast<int32>(FloorDiv(Box.Lo[UA], Steps));
					const int32 CU1 = static_cast<int32>(FloorDiv(Box.Hi[UA] - 1, Steps));
					const int32 CV0 = static_cast<int32>(FloorDiv(Box.Lo[VA], Steps));
					const int32 CV1 = static_cast<int32>(FloorDiv(Box.Hi[VA] - 1, Steps));
					for (int32 CV = CV0; CV <= CV1; ++CV)
					{
						for (int32 CU = CU0; CU <= CU1; ++CU)
						{
							const int64 TU0 = FMath::Max(Box.Lo[UA], CU * Steps);
							const int64 TU1 = FMath::Min(Box.Hi[UA], (CU + 1) * Steps);
							const int64 TV0 = FMath::Max(Box.Lo[VA], CV * Steps);
							const int64 TV1 = FMath::Min(Box.Hi[VA], (CV + 1) * Steps);
							Removed.Reset();
							auto Consider = [&](int32 Other, bool bCover)
							{
								const FQBox& B = Boxes[Other];
								const int64 U0 = FMath::Max(TU0, B.Lo[UA]);
								const int64 U1 = FMath::Min(TU1, B.Hi[UA]);
								const int64 V0 = FMath::Max(TV0, B.Lo[VA]);
								const int64 V1 = FMath::Min(TV1, B.Hi[VA]);
								if (U1 <= U0 || V1 <= V0)
								{
									return;
								}
								bool bTakes;
								if (bCover)
								{
									// B fills the space just in front of the face.
									bTakes = Sign > 0 ? (B.Lo[Axis] <= Plane && B.Hi[Axis] > Plane) : (B.Lo[Axis] < Plane && B.Hi[Axis] >= Plane);
								}
								else
								{
									// An earlier box with a face on the same plane, facing the same way, gives that part.
									bTakes = Other < BoxIndex && (Sign > 0 ? B.Hi[Axis] == Plane : B.Lo[Axis] == Plane);
								}
								if (bTakes)
								{
									Removed.Add({ U0, U1, V0, V1 });
								}
							};
							FIntVector Front;
							Front[Axis] = FrontCell;
							Front[UA] = CU;
							Front[VA] = CV;
							for (const int32 Other : BoxesIn(Front))
							{
								if (Other != BoxIndex)
								{
									Consider(Other, true);
								}
							}
							FIntVector Back = Front;
							Back[Axis] = BackCell;
							for (const int32 Other : BoxesIn(Back))
							{
								if (Other != BoxIndex)
								{
									Consider(Other, false);
								}
							}
							auto Emit = [&](int64 U0, int64 U1, int64 V0, int64 V1)
							{
								FRect& Rect = Rects.AddDefaulted_GetRef();
								Rect.Axis = Axis;
								Rect.Sign = Sign;
								Rect.Plane = Plane;
								Rect.U0 = U0; Rect.U1 = U1; Rect.V0 = V0; Rect.V1 = V1;
								Rect.Point = Box.Point;
								Rect.Class = ClassOf[Box.Point];
							};
							if (Removed.IsEmpty())
							{
								Emit(TU0, TU1, TV0, TV1);
								continue;
							}
							// Overlay of the tile and the removed parts; what is left, merged greedily into rectangles.
							UCuts = { TU0, TU1 };
							VCuts = { TV0, TV1 };
							for (const FCut& Cut : Removed)
							{
								UCuts.Add(Cut.U0); UCuts.Add(Cut.U1);
								VCuts.Add(Cut.V0); VCuts.Add(Cut.V1);
							}
							Algo::Sort(UCuts);
							Algo::Sort(VCuts);
							UCuts.SetNum(Algo::Unique(UCuts));
							VCuts.SetNum(Algo::Unique(VCuts));
							const int32 NU = UCuts.Num() - 1;
							const int32 NV = VCuts.Num() - 1;
							Open.Init(1, NU * NV);
							for (const FCut& Cut : Removed)
							{
								const int32 I0 = Algo::LowerBound(UCuts, Cut.U0);
								const int32 I1 = Algo::LowerBound(UCuts, Cut.U1);
								const int32 J0 = Algo::LowerBound(VCuts, Cut.V0);
								const int32 J1 = Algo::LowerBound(VCuts, Cut.V1);
								for (int32 J = J0; J < J1; ++J)
								{
									for (int32 I = I0; I < I1; ++I)
									{
										Open[I + NU * J] = 0;
									}
								}
							}
							for (int32 J = 0; J < NV; ++J)
							{
								for (int32 I = 0; I < NU; ++I)
								{
									if (!Open[I + NU * J])
									{
										continue;
									}
									int32 I1 = I + 1;
									while (I1 < NU && Open[I1 + NU * J]) { ++I1; }
									int32 J1 = J + 1;
									for (; J1 < NV; ++J1)
									{
										bool bRow = true;
										for (int32 K = I; K < I1 && bRow; ++K) { bRow = Open[K + NU * J1] != 0; }
										if (!bRow) { break; }
									}
									for (int32 JJ = J; JJ < J1; ++JJ)
									{
										for (int32 K = I; K < I1; ++K) { Open[K + NU * JJ] = 0; }
									}
									Emit(UCuts[I], UCuts[I1], VCuts[J], VCuts[J1]);
								}
							}
						}
					}
				}
			}
		}
	}
	const int32 NumPieces = Rects.Num();
	if (bCollapse)
	{
		JoinRects(Rects);
	}
	CellBoxes.Empty();
	EndStage(4);

	// ---- 3. Positions, welded; every axis-aligned line through them, so each edge picks up the vertices on it.
	TMap<FInt64Vector3, int32> PosIndex;
	TArray<FInt64Vector3> Positions;
	auto PosOf = [&](const FInt64Vector3& P) -> int32
	{
		if (const int32* Found = PosIndex.Find(P))
		{
			return *Found;
		}
		const int32 Index = Positions.Add(P);
		PosIndex.Add(P, Index);
		return Index;
	};
	auto Corner = [](const FRect& R, int64 U, int64 V)
	{
		FInt64Vector3 P;
		P[R.Axis] = R.Plane;
		P[PlaneU(R.Axis)] = U;
		P[PlaneV(R.Axis)] = V;
		return P;
	};
	for (const FRect& R : Rects)
	{
		PosOf(Corner(R, R.U0, R.V0));
		PosOf(Corner(R, R.U1, R.V0));
		PosOf(Corner(R, R.U1, R.V1));
		PosOf(Corner(R, R.U0, R.V1));
	}
	// Line key: (axis, the other two coordinates in axis order); value: the positions on it, sorted along the axis.
	TMap<FInt64Vector3, TArray<int32>> Lines;
	auto LineKey = [](int32 Axis, const FInt64Vector3& P)
	{
		return FInt64Vector3(Axis, P[PlaneU(Axis)], P[PlaneV(Axis)]);
	};
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Lines.FindOrAdd(LineKey(Axis, Positions[Index])).Add(Index);
		}
	}
	for (TPair<FInt64Vector3, TArray<int32>>& Line : Lines)
	{
		const int32 Axis = static_cast<int32>(Line.Key.X);
		Line.Value.Sort([&](int32 A, int32 B) { return Positions[A][Axis] < Positions[B][Axis]; });
	}

	// Polygons: each rectangle's outline counter-clockwise in (u, v), with every position lying on its edges.
	TArray<int32> PolyStart;
	TArray<int32> PolyPos;
	PolyStart.Reserve(Rects.Num() + 1);
	for (const FRect& R : Rects)
	{
		PolyStart.Add(PolyPos.Num());
		const int32 UA = PlaneU(R.Axis);
		const int32 VA = PlaneV(R.Axis);
		auto Walk = [&](const FInt64Vector3& From, const FInt64Vector3& To, int32 LineAxis)
		{
			PolyPos.Add(PosIndex.FindChecked(From));
			const TArray<int32>& Line = Lines.FindChecked(LineKey(LineAxis, From));
			const int64 A = From[LineAxis];
			const int64 B = To[LineAxis];
			if (A < B)
			{
				for (int32 K = Algo::UpperBoundBy(Line, A, [&](int32 P) { return Positions[P][LineAxis]; }); K < Line.Num() && Positions[Line[K]][LineAxis] < B; ++K)
				{
					PolyPos.Add(Line[K]);
				}
			}
			else
			{
				for (int32 K = Algo::LowerBoundBy(Line, A, [&](int32 P) { return Positions[P][LineAxis]; }) - 1; K >= 0 && Positions[Line[K]][LineAxis] > B; --K)
				{
					PolyPos.Add(Line[K]);
				}
			}
		};
		const FInt64Vector3 C0 = Corner(R, R.U0, R.V0), C1 = Corner(R, R.U1, R.V0), C2 = Corner(R, R.U1, R.V1), C3 = Corner(R, R.U0, R.V1);
		Walk(C0, C1, UA);
		Walk(C1, C2, VA);
		Walk(C2, C3, UA);
		Walk(C3, C0, VA);
	}
	PolyStart.Add(PolyPos.Num());
	Lines.Empty();
	EndStage(1);

	// ---- 4. Edges: the uses of each segment, paired (two at an ordinary edge; four at a pinch edge, paired by the
	// solid quarter they bound), and each pair's corners joined into vertices.
	const int32 NumUses = PolyPos.Num();
	FUnionFind Corners;
	Corners.Parent.Reserve(NumUses);
	for (int32 Use = 0; Use < NumUses; ++Use)
	{
		Corners.Add();
	}
	TArray<int32> RectOfUse;
	RectOfUse.SetNumUninitialized(NumUses);
	for (int32 RectID = 0; RectID < Rects.Num(); ++RectID)
	{
		for (int32 Use = PolyStart[RectID]; Use < PolyStart[RectID + 1]; ++Use)
		{
			RectOfUse[Use] = RectID;
		}
	}
	auto NextUse = [&](int32 Use)
	{
		const int32 RectID = RectOfUse[Use];
		return Use + 1 < PolyStart[RectID + 1] ? Use + 1 : PolyStart[RectID];
	};
	// A side of a segment: the use at its lower position and the use at its higher position.
	struct FSide { int32 LoUse; int32 HiUse; };
	TMap<uint64, TArray<FSide, TInlineAllocator<2>>> Segments;
	Segments.Reserve(NumUses / 2);
	for (int32 Use = 0; Use < NumUses; ++Use)
	{
		const int32 Next = NextUse(Use);
		const int32 A = PolyPos[Use];
		const int32 B = PolyPos[Next];
		const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
		Segments.FindOrAdd(Key).Add(A < B ? FSide{ Use, Next } : FSide{ Next, Use });
	}
	int32 NumOddEdges = 0;
	struct FSheet { FSide A; FSide B; };
	TArray<FSheet> Sheets;
	Sheets.Reserve(Segments.Num());
	for (TPair<uint64, TArray<FSide, TInlineAllocator<2>>>& Segment : Segments)
	{
		TArray<FSide, TInlineAllocator<2>>& Sides = Segment.Value;
		if (Sides.Num() == 4)
		{
			// Each face bounds one solid quarter round the segment: behind its normal, on the side it extends to.
			const FInt64Vector3& P0 = Positions[PolyPos[Sides[0].LoUse]];
			const FInt64Vector3& P1 = Positions[PolyPos[Sides[0].HiUse]];
			const int32 SegAxis = P0.X != P1.X ? 0 : (P0.Y != P1.Y ? 1 : 2);
			auto Quarter = [&](const FSide& Side)
			{
				const FRect& R = Rects[RectOfUse[Side.LoUse]];
				const int32 W = 3 - SegAxis - R.Axis;
				const int64 LineW = P0[W];
				const bool bLowW = (W == PlaneU(R.Axis) ? R.U0 : R.V0) == LineW;
				int32 Signs[3] = { 0, 0, 0 };
				Signs[R.Axis] = -R.Sign;
				Signs[W] = bLowW ? 1 : -1;
				return (Signs[0] + 1) * 9 + (Signs[1] + 1) * 3 + (Signs[2] + 1);
			};
			const int32 Q0 = Quarter(Sides[0]);
			int32 Mate = INDEX_NONE;
			for (int32 K = 1; K < 4 && Mate == INDEX_NONE; ++K)
			{
				if (Quarter(Sides[K]) == Q0) { Mate = K; }
			}
			if (Mate == INDEX_NONE)
			{
				Mate = 1;
				++NumOddEdges;
			}
			int32 Rest[2];
			int32 R = 0;
			for (int32 K = 1; K < 4; ++K)
			{
				if (K != Mate) { Rest[R++] = K; }
			}
			Sheets.Add({ Sides[0], Sides[Mate] });
			Sheets.Add({ Sides[Rest[0]], Sides[Rest[1]] });
		}
		else
		{
			if (Sides.Num() != 2)
			{
				++NumOddEdges;
			}
			for (int32 K = 0; K + 1 < Sides.Num(); K += 2)
			{
				Sheets.Add({ Sides[K], Sides[K + 1] });
			}
		}
	}
	Segments.Empty();
	for (const FSheet& Sheet : Sheets)
	{
		Corners.Union(Sheet.A.LoUse, Sheet.B.LoUse);
		Corners.Union(Sheet.A.HiUse, Sheet.B.HiUse);
	}

	// Vertices: one per joined corner group.
	TArray<int32> VertexOfUse;
	VertexOfUse.SetNumUninitialized(NumUses);
	TArray<int32> RootVertex;
	RootVertex.Init(INDEX_NONE, NumUses);
	TArray<FVector> VertexCells; // position in cells (lattice - 0.5 is the cell centre frame)
	for (int32 Use = 0; Use < NumUses; ++Use)
	{
		const int32 Root = Corners.Find(Use);
		if (RootVertex[Root] == INDEX_NONE)
		{
			RootVertex[Root] = VertexCells.Num();
			const FInt64Vector3& P = Positions[PolyPos[Use]];
			VertexCells.Add(FVector(static_cast<double>(P.X), static_cast<double>(P.Y), static_cast<double>(P.Z)) / static_cast<double>(Steps));
		}
		VertexOfUse[Use] = RootVertex[Root];
	}
	RootVertex.Empty();
	Corners.Parent.Empty();

	// Two sheets of a pinch edge that still share both end vertices would be one edge for three or four triangles: the
	// later sheet gets its own vertex at the segment's middle, on both of its faces.
	TMap<int32, int32> MidAfterUse; // use -> extra vertex placed after it in its polygon
	{
		TSet<uint64> EdgesSeen;
		EdgesSeen.Reserve(Sheets.Num());
		for (const FSheet& Sheet : Sheets)
		{
			const int32 VA = VertexOfUse[Sheet.A.LoUse];
			const int32 VB = VertexOfUse[Sheet.A.HiUse];
			const uint64 Key = (static_cast<uint64>(FMath::Min(VA, VB)) << 32) | static_cast<uint32>(FMath::Max(VA, VB));
			bool bSeen = false;
			EdgesSeen.Add(Key, &bSeen);
			if (!bSeen)
			{
				continue;
			}
			const int32 Mid = VertexCells.Add(0.5 * (VertexCells[VA] + VertexCells[VB]));
			for (const FSide& Side : { Sheet.A, Sheet.B })
			{
				// The polygon runs Lo -> Hi or Hi -> Lo; the extra vertex goes after whichever comes first.
				const int32 First = NextUse(Side.LoUse) == Side.HiUse ? Side.LoUse : Side.HiUse;
				MidAfterUse.Add(First, Mid);
			}
		}
	}
	Sheets.Empty();
	const int32 NumVertices = VertexCells.Num();
	EndStage(2);

	// ---- 5. Triangles: per polygon, ear clipping that never makes a zero-area triangle (clip a corner next to a
	// straight run first, so the run's points become corners in turn).
	const bool bFlip = (!MathCrossIsOutward()) != (Options.OutputSpace.GetDeterminant() < 0.0);
	TArray<int32> FinalStart;
	TArray<int32> FinalVertex;
	TArray<FIntVector> Tris; // local corner indices into the polygon
	TArray<int32> TriStart;
	FinalStart.Reserve(Rects.Num() + 1);
	TriStart.Reserve(Rects.Num() + 1);
	TArray<FVector2d, TInlineAllocator<64>> Flat;
	TArray<int32, TInlineAllocator<64>> Ring;
	for (int32 RectID = 0; RectID < Rects.Num(); ++RectID)
	{
		const FRect& R = Rects[RectID];
		const int32 Base = FinalVertex.Num();
		FinalStart.Add(Base);
		TriStart.Add(Tris.Num());
		for (int32 Use = PolyStart[RectID]; Use < PolyStart[RectID + 1]; ++Use)
		{
			FinalVertex.Add(VertexOfUse[Use]);
			if (const int32* Mid = MidAfterUse.Find(Use))
			{
				FinalVertex.Add(*Mid);
			}
		}
		const int32 N = FinalVertex.Num() - Base;
		const int32 UA = PlaneU(R.Axis);
		const int32 VA = PlaneV(R.Axis);
		Flat.Reset();
		for (int32 K = 0; K < N; ++K)
		{
			const FVector& C = VertexCells[FinalVertex[Base + K]];
			Flat.Add(FVector2d(C[UA], C[VA]));
		}
		const bool bReverse = (R.Sign < 0) != bFlip;
		auto AddTri = [&](int32 A, int32 B, int32 C) { Tris.Add(bReverse ? FIntVector(A, C, B) : FIntVector(A, B, C)); };
		Ring.Reset();
		for (int32 K = 0; K < N; ++K)
		{
			Ring.Add(K);
		}
		auto Cross = [&](int32 A, int32 B, int32 C)
		{
			const FVector2d AB = Flat[B] - Flat[A];
			const FVector2d BC = Flat[C] - Flat[B];
			return AB.X * BC.Y - AB.Y * BC.X;
		};
		auto IsFlat = [&](int32 At)
		{
			const int32 Num = Ring.Num();
			return Cross(Ring[(At + Num - 1) % Num], Ring[At], Ring[(At + 1) % Num]) == 0.0;
		};
		while (Ring.Num() > 3)
		{
			const int32 Num = Ring.Num();
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
				break;
			}
			AddTri(Ring[(Pick + Num - 1) % Num], Ring[Pick], Ring[(Pick + 1) % Num]);
			Ring.RemoveAt(Pick, EAllowShrinking::No);
		}
		if (Ring.Num() == 3 && Cross(Ring[0], Ring[1], Ring[2]) != 0.0)
		{
			AddTri(Ring[0], Ring[1], Ring[2]);
		}
	}
	FinalStart.Add(FinalVertex.Num());
	TriStart.Add(Tris.Num());
	MidAfterUse.Empty();

	// ---- 6. The mesh.
	OutMesh.Clear();
	OutMesh.EnableTriangleGroups();
	const FTransform& GridToWorld = Grid.GridToWorld;
	TArray<int32> MeshVertex;
	MeshVertex.SetNumUninitialized(NumVertices);
	for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
	{
		const FVector GridPosition = (VertexCells[Vertex] - FVector(0.5)) * Grid.VoxelSize;
		MeshVertex[Vertex] = OutMesh.AppendVertex(FVector3d(Options.OutputSpace.InverseTransformPosition(GridToWorld.TransformPositionNoScale(GridPosition))));
	}
	auto GroupOf = [&](int32 RectID) -> int32
	{
		const FRect& R = Rects[RectID];
		switch (Options.Polygroups)
		{
		case EPCGVoxelPolygroupMode::PerFaceDirection:   return R.Dir();
		case EPCGVoxelPolygroupMode::PerMaterial:        return SlotOf(R.Point);
		case EPCGVoxelPolygroupMode::PerCollapsedRegion: return RectID;
		case EPCGVoxelPolygroupMode::PerAttributeValue:  return Input.Polygroup.Num() == NumPoints ? Input.Polygroup[R.Point] : 0;
		default: return 0;
		}
	};
	TArray<int32> TriangleID;
	TriangleID.SetNumUninitialized(Tris.Num());
	int32 NumRejected = 0;
	for (int32 RectID = 0; RectID < Rects.Num(); ++RectID)
	{
		const int32 Base = FinalStart[RectID];
		const int32 Group = GroupOf(RectID);
		for (int32 Tri = TriStart[RectID]; Tri < TriStart[RectID + 1]; ++Tri)
		{
			const FIntVector& C = Tris[Tri];
			const int32 TID = OutMesh.AppendTriangle(MeshVertex[FinalVertex[Base + C.X]], MeshVertex[FinalVertex[Base + C.Y]], MeshVertex[FinalVertex[Base + C.Z]], Group);
			TriangleID[Tri] = TID;
			NumRejected += TID < 0 ? 1 : 0;
		}
	}
	EndStage(5);

	// ---- 7. Attributes: elements shared by (vertex, direction) for normals, grid UVs and hard colors, by (vertex,
	// material) for smooth colors.
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

	// Smooth colors: the average over the distinct points whose faces meet at a vertex (within a material when asked).
	TMap<uint64, FVector4f> SmoothColor;
	if (bSmooth)
	{
		TMap<uint64, TArray<int32, TInlineAllocator<8>>> PointsAt;
		for (int32 RectID = 0; RectID < Rects.Num(); ++RectID)
		{
			const int32 Point = Rects[RectID].Point;
			const uint32 Sub = bByMaterial ? static_cast<uint32>(SlotOf(Point)) : 0u;
			for (int32 K = FinalStart[RectID]; K < FinalStart[RectID + 1]; ++K)
			{
				PointsAt.FindOrAdd((static_cast<uint64>(FinalVertex[K]) << 32) | Sub).AddUnique(Point);
			}
		}
		for (const TPair<uint64, TArray<int32, TInlineAllocator<8>>>& At : PointsAt)
		{
			FVector4f Sum(0.0f, 0.0f, 0.0f, 0.0f);
			for (const int32 Point : At.Value)
			{
				Sum += Input.Color[Point];
			}
			SmoothColor.Add(At.Key, Sum / static_cast<float>(At.Value.Num()));
		}
	}

	TArray<int32> NormalElement;
	NormalElement.Init(INDEX_NONE, NumVertices * 6);
	TArray<int32> GridUVElement;
	if (UVs && Options.UVMode == EPCGVoxelUVMode::GridProjection)
	{
		GridUVElement.Init(INDEX_NONE, NumVertices * 6);
	}
	TArray<int32> HardColorElement;
	TArray<FVector4f> HardColorOf;
	TMap<uint64, int32> SmoothColorElement;
	if (Colors && !bSmooth)
	{
		HardColorElement.Init(INDEX_NONE, NumVertices * 6);
		HardColorOf.SetNumUninitialized(NumVertices * 6);
	}
	struct FCornerElements { int32 Normal = INDEX_NONE; int32 UV = INDEX_NONE; int32 Color = INDEX_NONE; };
	TArray<FCornerElements, TInlineAllocator<64>> CornerElements;
	for (int32 RectID = 0; RectID < Rects.Num(); ++RectID)
	{
		const FRect& R = Rects[RectID];
		const int32 Dir = R.Dir();
		const int32 Point = R.Point;
		const int32 Base = FinalStart[RectID];
		const int32 N = FinalStart[RectID + 1] - Base;
		const FVector3f Normal(FVector(Dir / 2 == 0 ? R.Sign : 0, Dir / 2 == 1 ? R.Sign : 0, Dir / 2 == 2 ? R.Sign : 0));
		auto CellsOf = [&](int32 Vertex) { return VertexCells[Vertex] - FVector(0.5); };
		FVector2d UVOrigin(TNumericLimits<double>::Max());
		if (UVs && GridUVElement.IsEmpty())
		{
			for (int32 K = 0; K < N; ++K)
			{
				const FVector2d Projected = Project(CellsOf(FinalVertex[Base + K]), Dir);
				UVOrigin = FVector2d(FMath::Min(UVOrigin.X, Projected.X), FMath::Min(UVOrigin.Y, Projected.Y));
			}
		}
		auto MakeUV = [&](const FVector& Cells) -> FVector2f
		{
			const FVector2d Projected = Project(Cells, Dir);
			switch (Options.UVMode)
			{
			case EPCGVoxelUVMode::GridProjection: return FVector2f(Projected * Options.UVScale);
			case EPCGVoxelUVMode::PerFace: return FVector2f(Projected - UVOrigin);
			case EPCGVoxelUVMode::AtlasFromAttribute:
			{
				const FVector2d Local = Projected - UVOrigin;
				const FVector4f Rect = bAtlas ? Input.AtlasRect[Point] : FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
				return FVector2f(Rect.X + Local.X * (Rect.Z - Rect.X), Rect.Y + Local.Y * (Rect.W - Rect.Y));
			}
			default: return FVector2f::ZeroVector;
			}
		};
		CornerElements.SetNum(N, EAllowShrinking::No);
		for (int32 K = 0; K < N; ++K)
		{
			const int32 Vertex = FinalVertex[Base + K];
			const int32 Key = Vertex * 6 + Dir;
			FCornerElements& Elements = CornerElements[K];
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
					const uint64 SmoothKey = (static_cast<uint64>(Vertex) << 32) | (bByMaterial ? static_cast<uint32>(SlotOf(Point)) : 0u);
					int32& ColorID = SmoothColorElement.FindOrAdd(SmoothKey, INDEX_NONE);
					if (ColorID == INDEX_NONE)
					{
						ColorID = Colors->AppendElement(SmoothColor.FindChecked(SmoothKey));
					}
					Elements.Color = ColorID;
				}
				else
				{
					const FVector4f& Color = Input.Color[Point];
					int32& ColorID = HardColorElement[Key];
					if (ColorID == INDEX_NONE)
					{
						ColorID = Colors->AppendElement(Color);
						HardColorOf[Key] = Color;
						Elements.Color = ColorID;
					}
					else
					{
						Elements.Color = HardColorOf[Key] == Color ? ColorID : Colors->AppendElement(Color);
					}
				}
			}
		}
		for (int32 Tri = TriStart[RectID]; Tri < TriStart[RectID + 1]; ++Tri)
		{
			const int32 TID = TriangleID[Tri];
			if (TID < 0)
			{
				continue;
			}
			const FIntVector& C = Tris[Tri];
			MaterialIDs->SetValue(TID, SlotOf(Point));
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
	// The output space may rotate or scale: normals follow the triangles, over the same hard splits.
	UE::Geometry::FMeshNormals::QuickRecomputeOverlayNormals(OutMesh);
	EndStage(6);

	if (OutStats)
	{
		OutStats->NumFaces = NumPieces;
		OutStats->NumRegions = Rects.Num();
		OutStats->NumRejectedTriangles = NumRejected + NumOddEdges;
	}
	return true;
}
