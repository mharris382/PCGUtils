// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshPlanarBoolean.h"

#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

namespace PCGUtilsPlanarBoolean
{
namespace PlanarBooleanPrivate
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex2i;
	using UE::Geometry::FIndex3i;

	inline double PlanarCross(const FVector2d& U, const FVector2d& V) { return U.X * V.Y - U.Y * V.X; }

	struct FPlanarBox
	{
		FVector2d Min = FVector2d(TNumericLimits<double>::Max());
		FVector2d Max = FVector2d(-TNumericLimits<double>::Max());

		bool IsValid() const { return Min.X <= Max.X && Min.Y <= Max.Y; }
		void Add(const FVector2d& P) { Min = FVector2d(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y)); Max = FVector2d(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y)); }
		void Add(const FPlanarBox& B) { if (B.IsValid()) { Add(B.Min); Add(B.Max); } }
		FPlanarBox Grown(double D) const { FPlanarBox R = *this; R.Min -= FVector2d(D); R.Max += FVector2d(D); return R; }
		bool Overlaps(const FPlanarBox& B) const { return IsValid() && B.IsValid() && Min.X <= B.Max.X && B.Min.X <= Max.X && Min.Y <= B.Max.Y && B.Min.Y <= Max.Y; }
		double MaxDim() const { return IsValid() ? FMath::Max(Max.X - Min.X, Max.Y - Min.Y) : 0.0; }
	};

	/** Buckets items by their 2D boxes. Query visits each item once. Not thread-safe (single Compute call). */
	struct FPlanarGrid
	{
		FVector2d Origin = FVector2d::ZeroVector;
		double Cell = 1.0;
		TMap<FIntPoint, TArray<int32>> Cells;
		/** Union of the boxes added; queries are clipped to it. */
		FPlanarBox Extent;
		mutable TArray<uint32> Stamp;
		mutable uint32 Epoch = 0;

		void Init(const FPlanarBox& Bounds, int32 NumItems, int32 MaxId)
		{
			Cells.Reset();
			Extent = FPlanarBox();
			Origin = Bounds.IsValid() ? Bounds.Min : FVector2d::ZeroVector;
			const double Area = Bounds.IsValid() ? FMath::Max((Bounds.Max.X - Bounds.Min.X) * (Bounds.Max.Y - Bounds.Min.Y), 1.0e-12) : 1.0;
			Cell = FMath::Max(FMath::Sqrt(Area / FMath::Max(NumItems, 1)) * 1.5, FMath::Max(Bounds.MaxDim() / 2048.0, 1.0e-6));
			Stamp.Init(0, FMath::Max(MaxId, 1));
			Epoch = 0;
		}

		FIntPoint CellOf(const FVector2d& P) const
		{
			return FIntPoint(FMath::FloorToInt32((P.X - Origin.X) / Cell), FMath::FloorToInt32((P.Y - Origin.Y) / Cell));
		}

		void Add(int32 Id, const FPlanarBox& Box)
		{
			Extent.Add(Box);
			const FIntPoint A = CellOf(Box.Min), B = CellOf(Box.Max);
			for (int32 Y = A.Y; Y <= B.Y; ++Y)
			{
				for (int32 X = A.X; X <= B.X; ++X)
				{
					Cells.FindOrAdd(FIntPoint(X, Y)).Add(Id);
				}
			}
		}

		template <typename FuncT>
		void Query(const FPlanarBox& InBox, FuncT&& Visit) const
		{
			if (!InBox.Overlaps(Extent)) { return; }
			FPlanarBox Box;
			Box.Min = FVector2d(FMath::Max(InBox.Min.X, Extent.Min.X), FMath::Max(InBox.Min.Y, Extent.Min.Y));
			Box.Max = FVector2d(FMath::Min(InBox.Max.X, Extent.Max.X), FMath::Min(InBox.Max.Y, Extent.Max.Y));
			if (++Epoch == 0) { for (uint32& S : Stamp) { S = 0; } Epoch = 1; }
			const FIntPoint A = CellOf(Box.Min), B = CellOf(Box.Max);
			for (int32 Y = A.Y; Y <= B.Y; ++Y)
			{
				for (int32 X = A.X; X <= B.X; ++X)
				{
					if (const TArray<int32>* Items = Cells.Find(FIntPoint(X, Y)))
					{
						for (const int32 Id : *Items)
						{
							if (Stamp[Id] == Epoch) { continue; }
							Stamp[Id] = Epoch;
							Visit(Id);
						}
					}
				}
			}
		}
	};

	enum class ETriState : uint8 { None, Valid, EdgeOn };

	/** One input seen on the plane. */
	struct FPlanarView
	{
		const FDynamicMesh3* Mesh = nullptr;
		int32 Index = 0;
		FString Label;
		TArray<FVector2d> P2;
		TArray<double> H;
		TArray<double> Area2;
		TArray<ETriState> State;
		double Sign = 1.0;
		bool bHasArea = false;
		FPlanarBox Bounds;
		FPlanarGrid TriGrid;
		/** Region boundary: edges with exactly one adjacent Valid triangle (mesh boundary, holes, the top of a skirt). */
		TArray<int32> BoundaryEdges;
		FPlanarGrid SegGrid;
		TArray<bool> Cut;
		TArray<int32> CutTriangles;
		/** Indices into the OTHER view's BoundaryEdges that cross this view's cut triangles. */
		TSet<int32> CrossingSegments;

		FPlanarBox TriBox(int32 Tid) const
		{
			const FIndex3i T = Mesh->GetTriangle(Tid);
			FPlanarBox B; B.Add(P2[T.A]); B.Add(P2[T.B]); B.Add(P2[T.C]);
			return B;
		}
		FVector2d Centroid(int32 Tid) const
		{
			const FIndex3i T = Mesh->GetTriangle(Tid);
			return (P2[T.A] + P2[T.B] + P2[T.C]) / 3.0;
		}
		bool Bary(int32 Tid, const FVector2d& P, FVector3d& Out) const
		{
			const FIndex3i T = Mesh->GetTriangle(Tid);
			const FVector2d A = P2[T.A], B = P2[T.B], C = P2[T.C];
			const double D = PlanarCross(B - A, C - A);
			if (FMath::Abs(D) <= TNumericLimits<double>::Min()) { return false; }
			const double WA = PlanarCross(B - P, C - P) / D;
			const double WB = PlanarCross(C - P, A - P) / D;
			Out = FVector3d(WA, WB, 1.0 - WA - WB);
			return true;
		}
		FVector3d Lift(int32 Tid, const FVector3d& W) const
		{
			const FIndex3i T = Mesh->GetTriangle(Tid);
			return Mesh->GetVertex(T.A) * W.X + Mesh->GetVertex(T.B) * W.Y + Mesh->GetVertex(T.C) * W.Z;
		}
	};

	inline double MinComponent(const FVector3d& V) { return FMath::Min3(V.X, V.Y, V.Z); }

	/** The Valid triangle containing P (bary >= -Eps), preferring the most interior one. */
	int32 PlanarLocate(const FPlanarView& V, const FVector2d& P, double Eps, FVector3d* OutBary = nullptr, const TArray<bool>* Only = nullptr)
	{
		FPlanarBox Box; Box.Add(P);
		int32 Best = INDEX_NONE;
		double BestMin = -Eps;
		FVector3d BestBary(0.0);
		V.TriGrid.Query(Box, [&](int32 Tid)
		{
			if (V.State[Tid] != ETriState::Valid || (Only && !(*Only)[Tid])) { return; }
			FVector3d W;
			if (!V.Bary(Tid, P, W)) { return; }
			const double M = MinComponent(W);
			if (M >= BestMin && (Best == INDEX_NONE || M > BestMin || Tid < Best))
			{
				Best = Tid; BestMin = M; BestBary = W;
			}
		});
		if (OutBary && Best != INDEX_NONE) { *OutBary = BestBary; }
		return Best;
	}

	/** Like PlanarLocate but always answers when any triangle is within Radius: the one P is least outside of. */
	int32 PlanarLocateNearest(const FPlanarView& V, const FVector2d& P, double Radius, FVector3d* OutBary, const TArray<bool>* Only = nullptr)
	{
		FPlanarBox Box; Box.Add(P);
		Box = Box.Grown(Radius);
		int32 Best = INDEX_NONE;
		double BestMin = -TNumericLimits<double>::Max();
		FVector3d BestBary(0.0);
		V.TriGrid.Query(Box, [&](int32 Tid)
		{
			if (V.State[Tid] != ETriState::Valid || (Only && !(*Only)[Tid])) { return; }
			FVector3d W;
			if (!V.Bary(Tid, P, W)) { return; }
			const double M = MinComponent(W);
			if (Best == INDEX_NONE || M > BestMin) { Best = Tid; BestMin = M; BestBary = W; }
		});
		if (OutBary && Best != INDEX_NONE) { *OutBary = BestBary; }
		return Best;
	}

	bool PlanarInside(const FPlanarView& V, const FVector2d& P)
	{
		return V.bHasArea && PlanarLocate(V, P, 1.0e-9) != INDEX_NONE;
	}

	FVector3d PlanarClampBary(FVector3d W)
	{
		W.X = FMath::Max(W.X, 0.0); W.Y = FMath::Max(W.Y, 0.0); W.Z = FMath::Max(W.Z, 0.0);
		const double S = W.X + W.Y + W.Z;
		if (S <= 0.0) { return FVector3d(1.0 / 3.0); }
		W /= S;
		for (int32 I = 0; I < 3; ++I) { if (W[I] < 1.0e-7) { W[I] = 0.0; } }
		return W / (W.X + W.Y + W.Z);
	}

	double PlanarPointSegDist2(const FVector2d& P, const FVector2d& A, const FVector2d& B, double* OutT = nullptr)
	{
		const FVector2d D = B - A;
		const double L2 = D.SquaredLength();
		double T = L2 > 0.0 ? FMath::Clamp((P - A).Dot(D) / L2, 0.0, 1.0) : 0.0;
		if (OutT) { *OutT = T; }
		return (A + D * T - P).SquaredLength();
	}

	double PlanarSegSegDist2(const FVector2d& P0, const FVector2d& P1, const FVector2d& Q0, const FVector2d& Q1)
	{
		const double D1 = PlanarCross(P1 - P0, Q0 - P0), D2 = PlanarCross(P1 - P0, Q1 - P0);
		const double D3 = PlanarCross(Q1 - Q0, P0 - Q0), D4 = PlanarCross(Q1 - Q0, P1 - Q0);
		if (((D1 > 0 && D2 < 0) || (D1 < 0 && D2 > 0)) && ((D3 > 0 && D4 < 0) || (D3 < 0 && D4 > 0)))
		{
			return 0.0;
		}
		return FMath::Min(FMath::Min(PlanarPointSegDist2(P0, Q0, Q1), PlanarPointSegDist2(P1, Q0, Q1)),
			FMath::Min(PlanarPointSegDist2(Q0, P0, P1), PlanarPointSegDist2(Q1, P0, P1)));
	}

	bool PlanarSegmentTouchesTriangle(const FPlanarView& V, int32 Tid, const FVector2d& P0, const FVector2d& P1, double Tol)
	{
		FVector3d W;
		if ((V.Bary(Tid, P0, W) && MinComponent(W) >= 0.0) || (V.Bary(Tid, P1, W) && MinComponent(W) >= 0.0)) { return true; }
		const FIndex3i T = V.Mesh->GetTriangle(Tid);
		const double Tol2 = Tol * Tol;
		return PlanarSegSegDist2(P0, P1, V.P2[T.A], V.P2[T.B]) <= Tol2 ||
			PlanarSegSegDist2(P0, P1, V.P2[T.B], V.P2[T.C]) <= Tol2 ||
			PlanarSegSegDist2(P0, P1, V.P2[T.C], V.P2[T.A]) <= Tol2;
	}

	/** -1 none, 0 A, 1 B. */
	int32 PlanarOwner(const FOptions& Options, bool bInA, bool bInB)
	{
		int32 Owner = INDEX_NONE;
		const int32 Winner = Options.bOverlapFromB ? 1 : 0;
		switch (Options.Operation)
		{
		case EOperation::Union:        Owner = (bInA && bInB) ? Winner : (bInA ? 0 : (bInB ? 1 : INDEX_NONE)); break;
		case EOperation::Intersection: Owner = (bInA && bInB) ? Winner : INDEX_NONE; break;
		case EOperation::Difference:   Owner = (bInA && !bInB) ? 0 : INDEX_NONE; break;
		case EOperation::ExclusiveOr:  Owner = (bInA != bInB) ? (bInA ? 0 : 1) : INDEX_NONE; break;
		}
		// Only a Cut operand can own area; a footprint or a whole operand is handled separately.
		if (Owner == 1 && Options.OperandMode != EOperandMode::Cut) { Owner = INDEX_NONE; }
		return Owner;
	}

	FString PlanarFmt(const FVector3d& P) { return FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), P.X, P.Y, P.Z); }

	struct FPlanarCorner
	{
		int32 S = 0;
		int32 Tri = INDEX_NONE;
		FVector3d Bary = FVector3d(1.0, 0.0, 0.0);
	};

	struct FPlanarOutVertex
	{
		int32 S = 0;
		int32 SrcVid = INDEX_NONE;
		FVector2d P2 = FVector2d::ZeroVector;
		FVector3d P3 = FVector3d::ZeroVector;
		/** Where the per-vertex attributes come from when SrcVid is not set. */
		FPlanarCorner Home;
	};

	struct FPlanarRecord
	{
		int32 V[3] = { 0, 0, 0 };
		int32 S = 0;
		int32 SrcTri = INDEX_NONE;
		FPlanarCorner C[3];
		bool bWall = false;
	};

	inline uint64 PlanarEdgeKey(int32 U, int32 W) { return U < W ? (uint64(uint32(U)) << 32) | uint32(W) : (uint64(uint32(W)) << 32) | uint32(U); }

	class FPlanarBooleanSolver
	{
	public:
		FPlanarBooleanSolver(const FDynamicMesh3& InA, const FDynamicMesh3& InB, const FPlanarFrame& InFrame, const FOptions& InOptions, FResult& InResult)
			: Frame(InFrame), Options(InOptions), Result(InResult)
		{
			Views[0].Mesh = &InA; Views[0].Index = 0; Views[0].Label = Options.LabelA;
			Views[1].Mesh = &InB; Views[1].Index = 1; Views[1].Label = Options.LabelB;
			Snap = FMath::Max(Options.SnapTolerance, 1.0e-9);
		}

		bool Run();

	private:
		FPlanarView Views[2];
		const FPlanarFrame& Frame;
		const FOptions& Options;
		FResult& Result;
		double Snap = 1.0e-3;

		TArray<FPlanarOutVertex> OutVerts;
		TMap<int32, int32> SourceVertexToOut[2];
		TArray<FPlanarRecord> Records;
		/** Output vertices on B's side of a stitch that are paired with a gap (not welded): A vertex -> B vertex. */
		TMap<int32, int32> GapPairs;

		void Fatal(const FString& Text) { Result.Messages.Add({ true, Text }); bFatal = true; }
		void ErrorKeep(const FString& Text) { Result.Messages.Add({ true, Text }); }
		void Warn(const FString& Text) { Result.Messages.Add({ false, Text }); }
		bool bFatal = false;

		void InitView(FPlanarView& V);
		void CheckCoplanar(const FPlanarView& V);
		void CheckFolds(const FPlanarView& V, const FPlanarBox* Restrict);
		void CheckStitchOperandOutline();
		void BuildBoundary(FPlanarView& V);
		void FindCut(FPlanarView& S, const FPlanarView& O);
		bool CanOwn(int32 SIndex) const;
		int32 AddSourceVertex(int32 S, int32 Vid);
		int32 AddNewVertex(int32 S, const FVector2d& P2, const FVector3d& P3, const FPlanarCorner& Home);
		void EmitWhole(const FPlanarView& S, int32 Tid);
		void OrientRecord(FPlanarRecord& R) const;
		FPlanarCorner LocateCorner(const FPlanarView& S, int32 OutVid, const FVector2d& Centroid) const;
		bool IsFeatureEdge(const FPlanarView& S, int32 Eid) const;
		void BuildPieces(FPlanarView& S, const FPlanarView& O);
		void RepairSeams();
		void Weld();
		void BuildWalls();
		void Assemble();
		void TransferVertexAttributes(FDynamicMesh3& Out) const;
	};

	void FPlanarBooleanSolver::InitView(FPlanarView& V)
	{
		const FDynamicMesh3& M = *V.Mesh;
		V.P2.SetNumZeroed(M.MaxVertexID());
		V.H.SetNumZeroed(M.MaxVertexID());
		for (const int32 Vid : M.VertexIndicesItr())
		{
			const FVector3d P = M.GetVertex(Vid);
			V.P2[Vid] = Frame.To2D(P);
			V.H[Vid] = Frame.Height(P);
		}
		V.Area2.Init(0.0, M.MaxTriangleID());
		V.State.Init(ETriState::None, M.MaxTriangleID());
		V.Cut.Init(false, M.MaxTriangleID());
		double Sum = 0.0;
		int32 NumValid = 0;
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			const FIndex3i T = M.GetTriangle(Tid);
			const FVector2d A = V.P2[T.A], B = V.P2[T.B], C = V.P2[T.C];
			const double A2 = PlanarCross(B - A, C - A);
			const double L2 = FMath::Max3((B - A).SquaredLength(), (C - B).SquaredLength(), (A - C).SquaredLength());
			V.Area2[Tid] = A2;
			const bool bEdgeOn = L2 <= 0.0 || FMath::Abs(A2) <= 1.0e-7 * L2;
			V.State[Tid] = bEdgeOn ? ETriState::EdgeOn : ETriState::Valid;
			if (!bEdgeOn) { Sum += A2; ++NumValid; V.Bounds.Add(V.TriBox(Tid)); }
		}
		V.Sign = Sum < 0.0 ? -1.0 : 1.0;
		V.bHasArea = NumValid > 0;
		V.TriGrid.Init(V.Bounds, NumValid, M.MaxTriangleID());
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			if (V.State[Tid] == ETriState::Valid) { V.TriGrid.Add(Tid, V.TriBox(Tid)); }
		}
	}

	void FPlanarBooleanSolver::CheckCoplanar(const FPlanarView& V)
	{
		const FDynamicMesh3& M = *V.Mesh;
		TArray<bool> Used; Used.Init(false, M.MaxVertexID());
		for (const int32 Tid : M.TriangleIndicesItr()) { const FIndex3i T = M.GetTriangle(Tid); Used[T.A] = Used[T.B] = Used[T.C] = true; }
		int32 Count = 0, Worst = INDEX_NONE;
		double WorstD = 0.0;
		for (const int32 Vid : M.VertexIndicesItr())
		{
			if (!Used[Vid]) { continue; }
			const double D = FMath::Abs(V.H[Vid]);
			if (D > Options.PlaneTolerance)
			{
				++Count;
				if (D > WorstD) { WorstD = D; Worst = Vid; }
			}
		}
		if (Count > 0)
		{
			Fatal(FString::Printf(TEXT("%s is not on the plane: vertex %d at %s is %.4f from it, more than Plane Tolerance %.4f. %d vertices are farther than the tolerance; the farthest is named."),
				*V.Label, Worst, *PlanarFmt(M.GetVertex(Worst)), WorstD, Options.PlaneTolerance, Count));
		}
	}

	void FPlanarBooleanSolver::CheckFolds(const FPlanarView& V, const FPlanarBox* Restrict)
	{
		const FDynamicMesh3& M = *V.Mesh;
		int32 Count = 0, First = INDEX_NONE;
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			if (V.State[Tid] != ETriState::Valid || FMath::Sign(V.Area2[Tid]) == V.Sign) { continue; }
			if (Restrict && !V.TriBox(Tid).Overlaps(*Restrict)) { continue; }
			if (First == INDEX_NONE) { First = Tid; }
			++Count;
		}
		if (Count > 0)
		{
			const FIndex3i T = M.GetTriangle(First);
			const FVector3d C = (M.GetVertex(T.A) + M.GetVertex(T.B) + M.GetVertex(T.C)) / 3.0;
			Fatal(FString::Printf(TEXT("%s folds along the projection: triangle %d (centre %s) faces the opposite way to the rest of the surface (projected area %.4f against a surface that is mostly %s), so the surface is not single-valued there (an overhang, a fold or a flipped triangle). %d triangles do this; the first is named."),
				*V.Label, First, *PlanarFmt(C), V.Area2[First] * 0.5, V.Sign > 0 ? TEXT("positive") : TEXT("negative"), Count));
		}
	}

	void FPlanarBooleanSolver::BuildBoundary(FPlanarView& V)
	{
		const FDynamicMesh3& M = *V.Mesh;
		for (const int32 Eid : M.EdgeIndicesItr())
		{
			const FIndex2i T = M.GetEdgeT(Eid);
			int32 N = 0;
			if (T.A != FDynamicMesh3::InvalidID && V.State[T.A] == ETriState::Valid) { ++N; }
			if (T.B != FDynamicMesh3::InvalidID && V.State[T.B] == ETriState::Valid) { ++N; }
			if (N == 1) { V.BoundaryEdges.Add(Eid); }
		}
		V.SegGrid.Init(V.Bounds, V.BoundaryEdges.Num(), V.BoundaryEdges.Num());
		for (int32 I = 0; I < V.BoundaryEdges.Num(); ++I)
		{
			const FIndex2i E = M.GetEdgeV(V.BoundaryEdges[I]);
			FPlanarBox B; B.Add(V.P2[E.A]); B.Add(V.P2[E.B]);
			V.SegGrid.Add(I, B);
		}
	}

	void FPlanarBooleanSolver::CheckStitchOperandOutline()
	{
		// Stitch joins A to B's open boundary. An edge-on triangle (a wall or a skirt) beside B's outline means
		// the outline is not B's mesh boundary, and welding A to it would give an edge three triangles.
		const FPlanarView& B = Views[1];
		const FDynamicMesh3& M = *B.Mesh;
		int32 Count = 0, First = INDEX_NONE;
		for (const int32 Eid : B.BoundaryEdges)
		{
			const FIndex2i T = M.GetEdgeT(Eid);
			for (const int32 Tid : { T.A, T.B })
			{
				if (Tid != FDynamicMesh3::InvalidID && B.State[Tid] == ETriState::EdgeOn)
				{
					if (First == INDEX_NONE) { First = Tid; }
					++Count;
				}
			}
		}
		if (Count > 0)
		{
			const FIndex3i T = M.GetTriangle(First);
			const FVector3d C = (M.GetVertex(T.A) + M.GetVertex(T.B) + M.GetVertex(T.C)) / 3.0;
			Fatal(FString::Printf(TEXT("%s has a wall or skirt along its outline: triangle %d (centre %s) is edge-on to the projection. Stitch joins A to B's open boundary, so B must be a surface without walls. Use B's surface-only output, or turn Stitch off. %d such triangles; the first is named."),
				*B.Label, First, *PlanarFmt(C), Count));
		}
	}

	void FPlanarBooleanSolver::FindCut(FPlanarView& S, const FPlanarView& O)
	{
		const FDynamicMesh3& M = *S.Mesh;
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			if (S.State[Tid] != ETriState::Valid) { continue; }
			const FPlanarBox Box = S.TriBox(Tid).Grown(Snap);
			bool bCut = false;
			O.SegGrid.Query(Box, [&](int32 SegIndex)
			{
				const FIndex2i E = O.Mesh->GetEdgeV(O.BoundaryEdges[SegIndex]);
				if (PlanarSegmentTouchesTriangle(S, Tid, O.P2[E.A], O.P2[E.B], Snap))
				{
					bCut = true;
					S.CrossingSegments.Add(SegIndex);
				}
			});
			if (bCut) { S.Cut[Tid] = true; S.CutTriangles.Add(Tid); }
		}
	}

	bool FPlanarBooleanSolver::CanOwn(int32 SIndex) const
	{
		if (SIndex == 0)
		{
			return !(Options.Operation == EOperation::Intersection && Options.bOverlapFromB && Options.OperandMode == EOperandMode::Cut);
		}
		if (Options.OperandMode != EOperandMode::Cut) { return false; }
		if (Options.Operation == EOperation::Difference) { return false; }
		if (Options.Operation == EOperation::Intersection && !Options.bOverlapFromB) { return false; }
		return true;
	}

	int32 FPlanarBooleanSolver::AddSourceVertex(int32 S, int32 Vid)
	{
		if (const int32* Found = SourceVertexToOut[S].Find(Vid)) { return *Found; }
		FPlanarOutVertex V;
		V.S = S; V.SrcVid = Vid; V.P2 = Views[S].P2[Vid]; V.P3 = Views[S].Mesh->GetVertex(Vid);
		const int32 Id = OutVerts.Add(V);
		SourceVertexToOut[S].Add(Vid, Id);
		return Id;
	}

	int32 FPlanarBooleanSolver::AddNewVertex(int32 S, const FVector2d& P2, const FVector3d& P3, const FPlanarCorner& Home)
	{
		FPlanarOutVertex V;
		V.S = S; V.P2 = P2; V.P3 = P3; V.Home = Home;
		return OutVerts.Add(V);
	}

	void FPlanarBooleanSolver::OrientRecord(FPlanarRecord& R) const
	{
		const FVector2d A = OutVerts[R.V[0]].P2, B = OutVerts[R.V[1]].P2, C = OutVerts[R.V[2]].P2;
		// Every output triangle faces like A (the output is A, mutated).
		const double Want = Views[0].bHasArea ? Views[0].Sign : Views[1].Sign;
		if (FMath::Sign(PlanarCross(B - A, C - A)) != Want)
		{
			Swap(R.V[1], R.V[2]);
			Swap(R.C[1], R.C[2]);
		}
	}

	void FPlanarBooleanSolver::EmitWhole(const FPlanarView& S, int32 Tid)
	{
		const FIndex3i T = S.Mesh->GetTriangle(Tid);
		FPlanarRecord R;
		R.S = S.Index; R.SrcTri = Tid;
		for (int32 K = 0; K < 3; ++K)
		{
			R.V[K] = AddSourceVertex(S.Index, T[K]);
			R.C[K].S = S.Index; R.C[K].Tri = Tid;
			R.C[K].Bary = FVector3d(K == 0 ? 1.0 : 0.0, K == 1 ? 1.0 : 0.0, K == 2 ? 1.0 : 0.0);
		}
		if (S.State[Tid] == ETriState::Valid) { OrientRecord(R); }
		else if (Views[0].bHasArea && S.Index == 1 && Views[1].Sign != Views[0].Sign) { Swap(R.V[1], R.V[2]); Swap(R.C[1], R.C[2]); }
		Records.Add(R);
	}

	FPlanarCorner FPlanarBooleanSolver::LocateCorner(const FPlanarView& S, int32 OutVid, const FVector2d& Centroid) const
	{
		FPlanarCorner C;
		C.S = S.Index;
		const FVector2d P = OutVerts[OutVid].P2;
		FVector3d W;
		int32 Tid = PlanarLocate(S, P, -1.0e-7, &W);
		if (Tid == INDEX_NONE)
		{
			// On an edge or a vertex of the source: use the source triangle on this corner's side.
			const FVector2d Q = P + (Centroid - P) * 1.0e-3;
			Tid = PlanarLocate(S, Q, 1.0e-9);
			if (Tid == INDEX_NONE) { Tid = PlanarLocateNearest(S, Q, Snap * 10.0 + (Centroid - P).Length(), nullptr); }
			if (Tid != INDEX_NONE) { S.Bary(Tid, P, W); }
		}
		if (Tid == INDEX_NONE) { Tid = PlanarLocateNearest(S, Centroid, (Centroid - P).Length() + Snap, &W); }
		C.Tri = Tid;
		C.Bary = PlanarClampBary(W);
		return C;
	}

	bool FPlanarBooleanSolver::IsFeatureEdge(const FPlanarView& S, int32 Eid) const
	{
		const FDynamicMesh3& M = *S.Mesh;
		const FIndex2i T = M.GetEdgeT(Eid);
		if (T.B == FDynamicMesh3::InvalidID) { return true; }
		if (M.HasTriangleGroups() && M.GetTriangleGroup(T.A) != M.GetTriangleGroup(T.B)) { return true; }
		// Keep creases: Delaunay only merges triangles that are flat to each other, so the surface never changes shape.
		if (M.GetTriNormal(T.A).Dot(M.GetTriNormal(T.B)) < 1.0 - 1.0e-6) { return true; }
		if (const UE::Geometry::FDynamicMeshAttributeSet* Attr = M.Attributes())
		{
			if (Attr->IsSeamEdge(Eid)) { return true; }
			if (const UE::Geometry::FDynamicMeshMaterialAttribute* Mat = Attr->GetMaterialID())
			{
				if (Mat->GetValue(T.A) != Mat->GetValue(T.B)) { return true; }
			}
			for (int32 L = 0; L < Attr->NumPolygroupLayers(); ++L)
			{
				const UE::Geometry::FDynamicMeshPolygroupAttribute* G = Attr->GetPolygroupLayer(L);
				if (G && G->GetValue(T.A) != G->GetValue(T.B)) { return true; }
			}
		}
		return false;
	}

	void FPlanarBooleanSolver::BuildPieces(FPlanarView& S, const FPlanarView& O)
	{
		using namespace UE::Geometry;
		const FDynamicMesh3& M = *S.Mesh;
		if (S.CutTriangles.IsEmpty()) { return; }

		FPlanarBox CutBox;
		double EdgeSum = 0.0; int32 EdgeCount = 0;
		for (const int32 Tid : S.CutTriangles)
		{
			CutBox.Add(S.TriBox(Tid));
			const FIndex3i T = M.GetTriangle(Tid);
			EdgeSum += (S.P2[T.A] - S.P2[T.B]).Length(); ++EdgeCount;
		}
		FArrangement2d Arr(FMath::Max(EdgeSum / FMath::Max(EdgeCount, 1), CutBox.MaxDim() / 256.0));
		Arr.VertexSnapTol = Snap;

		const bool bKeepEdges = Options.Triangulation == ETriangulation::KeepSourceEdges;
		TSet<int32> InsertedEdges;
		TSet<int32> EdgeEndpoints;
		for (const int32 Tid : S.CutTriangles)
		{
			const FIndex3i E = M.GetTriEdges(Tid);
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Eid = E[K];
				if (InsertedEdges.Contains(Eid)) { continue; }
				const FIndex2i ET = M.GetEdgeT(Eid);
				const int32 Other = ET.A == Tid ? ET.B : ET.A;
				const bool bOutline = Other == FDynamicMesh3::InvalidID || !S.Cut[Other] || S.State[Other] != ETriState::Valid;
				if (bKeepEdges || bOutline || IsFeatureEdge(S, Eid))
				{
					InsertedEdges.Add(Eid);
					const FIndex2i EV = M.GetEdgeV(Eid);
					EdgeEndpoints.Add(EV.A); EdgeEndpoints.Add(EV.B);
					Arr.Insert(S.P2[EV.A], S.P2[EV.B]);
				}
			}
		}
		for (const int32 SegIndex : S.CrossingSegments)
		{
			const FIndex2i EV = O.Mesh->GetEdgeV(O.BoundaryEdges[SegIndex]);
			Arr.Insert(O.P2[EV.A], O.P2[EV.B]);
		}
		if (!bKeepEdges)
		{
			// Interior source vertices stay as points, so values stored there are kept exactly.
			TSet<int32> Seen;
			for (const int32 Tid : S.CutTriangles)
			{
				const FIndex3i T = M.GetTriangle(Tid);
				for (int32 K = 0; K < 3; ++K)
				{
					const int32 Vid = T[K];
					if (Seen.Contains(Vid) || EdgeEndpoints.Contains(Vid)) { continue; }
					Seen.Add(Vid);
					bool bInterior = true;
					M.EnumerateVertexTriangles(Vid, [&](int32 RingTid) { bInterior &= S.Cut[RingTid] && S.State[RingTid] == ETriState::Valid; });
					if (!bInterior) { continue; }
					const bool bInO = PlanarInside(O, S.P2[Vid]);
					if (PlanarOwner(Options, S.Index == 0 ? true : bInO, S.Index == 0 ? bInO : true) == S.Index)
					{
						Arr.Insert(S.P2[Vid]);
					}
				}
			}
		}

		TConstrainedDelaunay2<double> CDT;
		CDT.bOrientedEdges = false;
		TArray<int32> GraphToCDT; GraphToCDT.Init(INDEX_NONE, Arr.Graph.MaxVertexID());
		TArray<int32> CDTToGraph;
		for (int32 G = 0; G < Arr.Graph.MaxVertexID(); ++G)
		{
			if (!Arr.Graph.IsVertex(G)) { continue; }
			GraphToCDT[G] = CDT.Vertices.Add(Arr.Graph.GetVertex(G));
			CDTToGraph.Add(G);
		}
		for (int32 Eid = 0; Eid < Arr.Graph.MaxEdgeID(); ++Eid)
		{
			if (!Arr.Graph.IsEdge(Eid)) { continue; }
			const FIndex2i EV = Arr.Graph.GetEdgeV(Eid);
			CDT.Edges.Add(FIndex2i(GraphToCDT[EV.A], GraphToCDT[EV.B]));
		}
		if (CDT.Vertices.Num() < 3) { return; }

		// TConstrainedDelaunay2 calls KeepTriangle from a ParallelFor, and the grids here are single-threaded, so
		// keep every triangle there and decide ownership in the loop below.
		const bool bOK = CDT.Triangulate([](const TArray<FVector2d>&, const FIndex3i&) { return true; });
		if (!bOK)
		{
			ErrorKeep(FString::Printf(TEXT("%s: the constrained triangulation of its cut area (%d source triangles, %d crossing outline segments of %s) reported a failure; some of the cut area may be missing. Try a larger Snap Tolerance."),
				*S.Label, S.CutTriangles.Num(), S.CrossingSegments.Num(), *O.Label));
		}

		TArray<int32> GraphToOut; GraphToOut.Init(INDEX_NONE, Arr.Graph.MaxVertexID());
		auto MapVertex = [&](int32 G, const FVector2d& Centroid) -> int32
		{
			if (GraphToOut[G] != INDEX_NONE) { return GraphToOut[G]; }
			const FVector2d P = Arr.Graph.GetVertex(G);
			// A point on a source vertex is that vertex.
			int32 SourceVid = INDEX_NONE;
			FPlanarBox Box; Box.Add(P);
			S.TriGrid.Query(Box.Grown(Snap), [&](int32 Tid)
			{
				if (SourceVid != INDEX_NONE) { return; }
				const FIndex3i T = M.GetTriangle(Tid);
				for (int32 K = 0; K < 3; ++K)
				{
					if ((S.P2[T[K]] - P).SquaredLength() <= Snap * Snap) { SourceVid = T[K]; return; }
				}
			});
			int32 Id;
			if (SourceVid != INDEX_NONE)
			{
				Id = AddSourceVertex(S.Index, SourceVid);
			}
			else
			{
				FVector3d W;
				int32 Tid = PlanarLocate(S, P, 1.0e-7, &W, &S.Cut);
				if (Tid == INDEX_NONE) { Tid = PlanarLocateNearest(S, P, Snap * 10.0 + (Centroid - P).Length(), &W, &S.Cut); }
				FPlanarCorner Home;
				Home.S = S.Index; Home.Tri = Tid; Home.Bary = PlanarClampBary(W);
				const FVector3d P3 = Tid != INDEX_NONE ? S.Lift(Tid, Home.Bary) : Frame.Origin + Frame.AxisX * P.X + Frame.AxisY * P.Y;
				Id = AddNewVertex(S.Index, P, P3, Home);
			}
			GraphToOut[G] = Id;
			return Id;
		};

		for (const FIndex3i& T : CDT.Triangles)
		{
			const int32 G[3] = { CDTToGraph[T.A], CDTToGraph[T.B], CDTToGraph[T.C] };
			const FVector2d C = (Arr.Graph.GetVertex(G[0]) + Arr.Graph.GetVertex(G[1]) + Arr.Graph.GetVertex(G[2])) / 3.0;
			FPlanarRecord R;
			R.S = S.Index;
			R.SrcTri = PlanarLocate(S, C, 1.0e-9, nullptr, &S.Cut);
			if (R.SrcTri == INDEX_NONE) { continue; }
			const bool bInO = PlanarInside(O, C);
			if (PlanarOwner(Options, S.Index == 0 ? true : bInO, S.Index == 0 ? bInO : true) != S.Index) { continue; }
			for (int32 K = 0; K < 3; ++K) { R.V[K] = MapVertex(G[K], C); }
			if (R.V[0] == R.V[1] || R.V[1] == R.V[2] || R.V[0] == R.V[2]) { continue; }
			for (int32 K = 0; K < 3; ++K) { R.C[K] = LocateCorner(S, R.V[K], C); if (R.C[K].Tri == INDEX_NONE) { R.C[K].Tri = R.SrcTri; } }
			OrientRecord(R);
			Records.Add(R);
		}
	}

	void FPlanarBooleanSolver::RepairSeams()
	{
		// A vertex of one input lying inside a boundary edge of the other (a T-junction on the seam) splits that edge.
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			TMap<uint64, int32> EdgeUse;
			TMap<uint64, TPair<int32, int32>> EdgeOwner; // record, corner
			for (int32 RI = 0; RI < Records.Num(); ++RI)
			{
				for (int32 K = 0; K < 3; ++K)
				{
					const uint64 Key = PlanarEdgeKey(Records[RI].V[K], Records[RI].V[(K + 1) % 3]);
					EdgeUse.FindOrAdd(Key)++;
					EdgeOwner.Add(Key, TPair<int32, int32>(RI, K));
				}
			}
			TSet<int32> BoundaryVerts;
			FPlanarBox Bounds;
			for (const TPair<uint64, int32>& E : EdgeUse)
			{
				if (E.Value != 1) { continue; }
				const int32 U = int32(E.Key >> 32), W = int32(E.Key & 0xffffffffu);
				BoundaryVerts.Add(U); BoundaryVerts.Add(W);
				Bounds.Add(OutVerts[U].P2); Bounds.Add(OutVerts[W].P2);
			}
			if (BoundaryVerts.IsEmpty()) { return; }
			TArray<int32> VertList = BoundaryVerts.Array();
			FPlanarGrid Grid;
			Grid.Init(Bounds, VertList.Num(), VertList.Num());
			for (int32 I = 0; I < VertList.Num(); ++I) { FPlanarBox B; B.Add(OutVerts[VertList[I]].P2); Grid.Add(I, B); }

			struct FSplit { int32 Corner; double T; int32 Vertex; };
			TMap<int32, TArray<FSplit>> Splits;
			for (const TPair<uint64, int32>& E : EdgeUse)
			{
				if (E.Value != 1) { continue; }
				const TPair<int32, int32> Own = EdgeOwner[E.Key];
				const FPlanarRecord& R = Records[Own.Key];
				if (TArray<FSplit>* Existing = Splits.Find(Own.Key); Existing && Existing->Num() > 0 && (*Existing)[0].Corner != Own.Value) { continue; }
				const int32 U = R.V[Own.Value], W = R.V[(Own.Value + 1) % 3];
				const FVector2d PU = OutVerts[U].P2, PW = OutVerts[W].P2;
				FPlanarBox Box; Box.Add(PU); Box.Add(PW);
				Grid.Query(Box.Grown(Snap), [&](int32 I)
				{
					const int32 V = VertList[I];
					if (V == U || V == W || OutVerts[V].S == R.S) { return; }
					const FVector2d P = OutVerts[V].P2;
					if ((P - PU).SquaredLength() <= Snap * Snap || (P - PW).SquaredLength() <= Snap * Snap) { return; }
					double T;
					if (PlanarPointSegDist2(P, PU, PW, &T) > Snap * Snap || T <= 0.0 || T >= 1.0) { return; }
					Splits.FindOrAdd(Own.Key).Add({ Own.Value, T, V });
				});
			}
			if (Splits.IsEmpty()) { return; }

			TArray<FPlanarRecord> NewRecords;
			TArray<bool> Replaced; Replaced.Init(false, Records.Num());
			for (TPair<int32, TArray<FSplit>>& Entry : Splits)
			{
				const FPlanarRecord R = Records[Entry.Key];
				TArray<FSplit>& List = Entry.Value;
				List.Sort([](const FSplit& X, const FSplit& Y) { return X.T < Y.T; });
				const int32 K0 = List[0].Corner, K1 = (K0 + 1) % 3, K2 = (K0 + 2) % 3;
				const int32 U = R.V[K0], W = R.V[K1];
				TArray<int32> Chain = { U };
				TArray<FPlanarCorner> ChainCorners = { R.C[K0] };
				double LastT = 0.0;
				for (const FSplit& Sp : List)
				{
					if (Sp.T - LastT <= 1.0e-9) { continue; }
					LastT = Sp.T;
					const FVector2d P2 = FMath::Lerp(OutVerts[U].P2, OutVerts[W].P2, Sp.T);
					const FVector3d P3 = FMath::Lerp(OutVerts[U].P3, OutVerts[W].P3, Sp.T);
					FPlanarCorner C;
					C.S = R.C[K0].S;
					C.Tri = R.C[K0].Tri;
					if (R.C[K0].Tri == R.C[K1].Tri && R.C[K0].S == R.C[K1].S) { C.Bary = PlanarClampBary(FMath::Lerp(R.C[K0].Bary, R.C[K1].Bary, Sp.T)); }
					else { FVector3d W3; Views[C.S].Bary(C.Tri, P2, W3); C.Bary = PlanarClampBary(W3); }
					Chain.Add(AddNewVertex(R.S, P2, P3, C));
					ChainCorners.Add(C);
				}
				Chain.Add(W);
				ChainCorners.Add(R.C[K1]);
				for (int32 I = 0; I + 1 < Chain.Num(); ++I)
				{
					FPlanarRecord N = R;
					N.V[0] = Chain[I]; N.V[1] = Chain[I + 1]; N.V[2] = R.V[K2];
					N.C[0] = ChainCorners[I]; N.C[1] = ChainCorners[I + 1]; N.C[2] = R.C[K2];
					NewRecords.Add(N);
				}
				Replaced[Entry.Key] = true;
			}
			TArray<FPlanarRecord> Kept;
			Kept.Reserve(Records.Num() + NewRecords.Num());
			for (int32 RI = 0; RI < Records.Num(); ++RI) { if (!Replaced[RI]) { Kept.Add(Records[RI]); } }
			Kept.Append(NewRecords);
			Records = MoveTemp(Kept);
		}
	}

	void FPlanarBooleanSolver::Weld()
	{
		TMap<uint64, int32> EdgeUse;
		for (const FPlanarRecord& R : Records)
		{
			for (int32 K = 0; K < 3; ++K) { EdgeUse.FindOrAdd(PlanarEdgeKey(R.V[K], R.V[(K + 1) % 3]))++; }
		}
		TSet<int32> BoundaryVerts;
		for (const TPair<uint64, int32>& E : EdgeUse)
		{
			if (E.Value == 1) { BoundaryVerts.Add(int32(E.Key >> 32)); BoundaryVerts.Add(int32(E.Key & 0xffffffffu)); }
		}
		TArray<int32> BVerts;
		FPlanarBox Bounds;
		for (const int32 V : BoundaryVerts) { if (OutVerts[V].S == 1) { BVerts.Add(V); Bounds.Add(OutVerts[V].P2); } }
		if (BVerts.IsEmpty()) { return; }
		BVerts.Sort();
		FPlanarGrid Grid;
		Grid.Init(Bounds, BVerts.Num(), BVerts.Num());
		for (int32 I = 0; I < BVerts.Num(); ++I) { FPlanarBox B; B.Add(OutVerts[BVerts[I]].P2); Grid.Add(I, B); }

		TArray<int32> AVerts;
		for (const int32 V : BoundaryVerts) { if (OutVerts[V].S == 0) { AVerts.Add(V); } }
		AVerts.Sort();

		const bool bStitch = Options.OperandMode == EOperandMode::Whole;
		TArray<int32> Remap; Remap.SetNumUninitialized(OutVerts.Num());
		for (int32 I = 0; I < Remap.Num(); ++I) { Remap[I] = I; }
		TSet<int32> UsedB;
		for (const int32 A : AVerts)
		{
			int32 Best = INDEX_NONE; double BestD = Snap * Snap;
			FPlanarBox Box; Box.Add(OutVerts[A].P2);
			Grid.Query(Box.Grown(Snap), [&](int32 I)
			{
				const int32 B = BVerts[I];
				if (UsedB.Contains(B)) { return; }
				const double D = (OutVerts[B].P2 - OutVerts[A].P2).SquaredLength();
				if (D <= BestD) { BestD = D; Best = B; }
			});
			if (Best == INDEX_NONE) { continue; }
			UsedB.Add(Best);
			const double Gap = FMath::Abs(Frame.Height(OutVerts[Best].P3) - Frame.Height(OutVerts[A].P3));
			if (bStitch && Gap > Options.SeamTolerance)
			{
				GapPairs.Add(A, Best);
				continue;
			}
			// Keep A's vertex id (the output is A, mutated) and take the chosen position.
			if (Options.WeldPosition == EWeldPosition::KeepB) { OutVerts[A].P3 = OutVerts[Best].P3; }
			Remap[Best] = A;
			++Result.SeamWelds;
		}
		if (Result.SeamWelds == 0) { return; }
		TArray<FPlanarRecord> Kept;
		Kept.Reserve(Records.Num());
		for (FPlanarRecord R : Records)
		{
			for (int32 K = 0; K < 3; ++K) { R.V[K] = Remap[R.V[K]]; }
			if (R.V[0] == R.V[1] || R.V[1] == R.V[2] || R.V[0] == R.V[2]) { continue; }
			Kept.Add(R);
		}
		Records = MoveTemp(Kept);
	}

	void FPlanarBooleanSolver::BuildWalls()
	{
		if (GapPairs.IsEmpty()) { return; }
		Result.SeamGapVertices = GapPairs.Num();

		// Directed boundary edges, by input.
		TMap<uint64, int32> EdgeUse;
		for (const FPlanarRecord& R : Records)
		{
			for (int32 K = 0; K < 3; ++K) { EdgeUse.FindOrAdd(PlanarEdgeKey(R.V[K], R.V[(K + 1) % 3]))++; }
		}
		TSet<TPair<int32, int32>> DirectedB;
		TArray<TPair<TPair<int32, int32>, int32>> DirectedA; // (from, to), record
		TMap<int32, FPlanarCorner> HomeOf;
		for (int32 RI = 0; RI < Records.Num(); ++RI)
		{
			const FPlanarRecord& R = Records[RI];
			for (int32 K = 0; K < 3; ++K)
			{
				HomeOf.FindOrAdd(R.V[K]) = R.C[K];
				const int32 U = R.V[K], W = R.V[(K + 1) % 3];
				if (EdgeUse[PlanarEdgeKey(U, W)] != 1) { continue; }
				if (R.S == 1) { DirectedB.Add(TPair<int32, int32>(U, W)); }
				else { DirectedA.Add(TPair<TPair<int32, int32>, int32>(TPair<int32, int32>(U, W), RI)); }
			}
		}

		// Seam vertices of A: welded ones are themselves; gap ones point at their B partner.
		auto Partner = [&](int32 A, int32& OutB, bool& bWelded) -> bool
		{
			if (const int32* B = GapPairs.Find(A)) { OutB = *B; bWelded = false; return true; }
			return false;
		};

		// Gap runs for reporting: A gap vertices joined by A boundary edges between two gap vertices.
		TMap<int32, int32> Parent;
		auto FindRoot = [&Parent](int32 X) -> int32
		{
			int32 Root = X;
			while (true)
			{
				const int32 P = Parent.FindOrAdd(Root, Root);
				if (P == Root) { break; }
				Root = P;
			}
			while (X != Root) { const int32 Next = Parent[X]; Parent[X] = Root; X = Next; }
			return Root;
		};
		for (const TPair<int32, int32>& Pair : GapPairs) { FindRoot(Pair.Key); }
		for (const TPair<TPair<int32, int32>, int32>& E : DirectedA)
		{
			if (GapPairs.Contains(E.Key.Key) && GapPairs.Contains(E.Key.Value))
			{
				const int32 RA = FindRoot(E.Key.Key), RB = FindRoot(E.Key.Value);
				if (RA != RB) { Parent[RA] = RB; }
			}
		}
		struct FRun { int32 Count = 0; double MaxGap = 0.0; FVector3d Where = FVector3d::ZeroVector; };
		TMap<int32, FRun> Runs;
		for (const TPair<int32, int32>& Pair : GapPairs)
		{
			FRun& Run = Runs.FindOrAdd(FindRoot(Pair.Key));
			++Run.Count;
			const double Gap = FMath::Abs(Frame.Height(OutVerts[Pair.Value].P3) - Frame.Height(OutVerts[Pair.Key].P3));
			if (Gap > Run.MaxGap) { Run.MaxGap = Gap; Run.Where = OutVerts[Pair.Key].P3; }
		}
		int32 RunIndex = 0;
		for (const TPair<int32, FRun>& Run : Runs)
		{
			++RunIndex;
			if (RunIndex > 20) { break; }
			const FString Text = FString::Printf(TEXT("Seam between %s and %s: a run of %d seam vertices is open by up to %.4f along the projection at %s (mesh space), more than Seam Tolerance %.4f. %s"),
				*Options.LabelA, *Options.LabelB, Run.Value.Count, Run.Value.MaxGap, *PlanarFmt(Run.Value.Where), Options.SeamTolerance,
				Options.bFillSeamGaps ? TEXT("Filled with a vertical wall.") : TEXT("Left open because Fill Seam Gaps is off."));
			if (Options.bFillSeamGaps) { Warn(Text); } else { ErrorKeep(Text); }
		}
		if (Runs.Num() > 20)
		{
			Warn(FString::Printf(TEXT("Seam between %s and %s: %d more gap runs are not listed."), *Options.LabelA, *Options.LabelB, Runs.Num() - 20));
		}
		if (!Options.bFillSeamGaps) { return; }

		for (const TPair<TPair<int32, int32>, int32>& E : DirectedA)
		{
			const int32 A0 = E.Key.Key, A1 = E.Key.Value;
			int32 B0 = A0, B1 = A1; bool bW0 = true, bW1 = true;
			const bool bP0 = Partner(A0, B0, bW0), bP1 = Partner(A1, B1, bW1);
			if (!bP0 && !bP1) { continue; }
			// A vertex with no gap partner is a welded seam vertex only if it is shared with B's boundary.
			if (!bP0 && !DirectedB.Contains(TPair<int32, int32>(B1, A0))) { continue; }
			if (!bP1 && !DirectedB.Contains(TPair<int32, int32>(A1, B0))) { continue; }
			if (bP0 && bP1 && !DirectedB.Contains(TPair<int32, int32>(B1, B0))) { continue; }

			const FPlanarRecord& Src = Records[E.Value];
			auto MakeWall = [&](int32 X, int32 Y, int32 Z)
			{
				FPlanarRecord W;
				W.S = Src.S; W.SrcTri = Src.SrcTri; W.bWall = true;
				W.V[0] = X; W.V[1] = Y; W.V[2] = Z;
				for (int32 K = 0; K < 3; ++K) { if (const FPlanarCorner* H = HomeOf.Find(W.V[K])) { W.C[K] = *H; } else { W.C[K].S = Src.S; W.C[K].Tri = Src.SrcTri; } }
				Records.Add(W);
				++Result.WallTriangles;
			};
			if (!bP0) { MakeWall(A1, A0, B1); }        // A0 welded to B0
			else if (!bP1) { MakeWall(A1, A0, B0); }   // A1 welded to B1
			else { MakeWall(A1, A0, B0); MakeWall(A1, B0, B1); }
		}
	}

	/** Output overlay element identity: one output vertex, one source, one set of contributing source elements. */
	struct FPlanarOverlayKey
	{
		int32 V = 0, S = 0, E0 = -1, E1 = -1, E2 = -1, Wall = -1;
		bool operator==(const FPlanarOverlayKey& O) const { return V == O.V && S == O.S && E0 == O.E0 && E1 == O.E1 && E2 == O.E2 && Wall == O.Wall; }
		friend uint32 GetTypeHash(const FPlanarOverlayKey& K)
		{
			uint32 H = ::GetTypeHash(K.V);
			H = HashCombineFast(H, ::GetTypeHash(K.S)); H = HashCombineFast(H, ::GetTypeHash(K.E0));
			H = HashCombineFast(H, ::GetTypeHash(K.E1)); H = HashCombineFast(H, ::GetTypeHash(K.E2));
			return HashCombineFast(H, ::GetTypeHash(K.Wall));
		}
	};

	template <typename RealType, int ElementSize, typename VectorType>
	void PlanarTransferOverlay(UE::Geometry::TDynamicMeshVectorOverlay<RealType, ElementSize, VectorType>* Out,
		const UE::Geometry::TDynamicMeshVectorOverlay<RealType, ElementSize, VectorType>* SrcA,
		const UE::Geometry::TDynamicMeshVectorOverlay<RealType, ElementSize, VectorType>* SrcB,
		const FDynamicMesh3& OutMesh, const TArray<FPlanarRecord>& Recs, const TArray<int32>& RecordTid,
		EVertexInheritance Mode, bool bNormal, bool bFlipB, const TArray<FVector3f>& VertexNormals)
	{
		using FKey = FPlanarOverlayKey;
		TMap<FKey, int32> Elements;
		constexpr bool bIsVector3 = std::is_same_v<VectorType, FVector3f>;
		auto Zero = []() { VectorType V; FMemory::Memzero(&V, sizeof(VectorType)); return V; };
		for (int32 RI = 0; RI < Recs.Num(); ++RI)
		{
			const int32 Tid = RecordTid[RI];
			if (Tid < 0) { continue; }
			const FPlanarRecord& R = Recs[RI];
			const FIndex3i TV = OutMesh.GetTriangle(Tid);
			int32 E[3];
			for (int32 K = 0; K < 3; ++K)
			{
				const FPlanarCorner& C = R.C[K];
				FKey Key; Key.V = TV[K]; Key.S = C.S;
				VectorType Value = Zero();
				const auto* Src = C.S == 0 ? SrcA : SrcB;
				const bool bHasSrc = Src && C.Tri >= 0 && Src->IsSetTriangle(C.Tri);
				if (bNormal && R.bWall)
				{
					Key.Wall = Tid; Key.S = -1;
					if constexpr (bIsVector3) { Value = FVector3f(OutMesh.GetTriNormal(Tid)); }
				}
				else if (Mode == EVertexInheritance::None || !bHasSrc)
				{
					Key.S = -1;
					if constexpr (bIsVector3) { if (bNormal && VertexNormals.IsValidIndex(TV[K])) { Value = VertexNormals[TV[K]]; } }
				}
				else
				{
					const FIndex3i SE = Src->GetTriangle(C.Tri);
					if (Mode == EVertexInheritance::Nearest)
					{
						const int32 I = C.Bary.X >= C.Bary.Y && C.Bary.X >= C.Bary.Z ? 0 : (C.Bary.Y >= C.Bary.Z ? 1 : 2);
						Key.E0 = SE[I];
						Value = Src->GetElement(SE[I]);
					}
					else
					{
						TArray<TPair<int32, double>, TInlineAllocator<3>> W;
						for (int32 I = 0; I < 3; ++I)
						{
							if (C.Bary[I] <= 1.0e-7) { continue; }
							bool bMerged = false;
							for (TPair<int32, double>& Existing : W) { if (Existing.Key == SE[I]) { Existing.Value += C.Bary[I]; bMerged = true; } }
							if (!bMerged) { W.Add(TPair<int32, double>(SE[I], C.Bary[I])); }
						}
						W.Sort([](const TPair<int32, double>& X, const TPair<int32, double>& Y) { return X.Key < Y.Key; });
						for (int32 I = 0; I < W.Num(); ++I)
						{
							(I == 0 ? Key.E0 : (I == 1 ? Key.E1 : Key.E2)) = W[I].Key;
							Value += Src->GetElement(W[I].Key) * float(W[I].Value);
						}
					}
					if constexpr (bIsVector3)
					{
						if (bNormal)
						{
							Value = Value.GetSafeNormal();
							if (bFlipB && C.S == 1) { Value = -Value; }
						}
					}
				}
				if (const int32* Found = Elements.Find(Key)) { E[K] = *Found; }
				else { E[K] = Out->AppendElement(Value); Elements.Add(Key, E[K]); }
			}
			Out->SetTriangle(Tid, FIndex3i(E[0], E[1], E[2]));
		}
	}

	void FPlanarBooleanSolver::TransferVertexAttributes(FDynamicMesh3& Out) const
	{
		const FDynamicMesh3& A = *Views[0].Mesh;
		const FDynamicMesh3& B = *Views[1].Mesh;
		const bool bNone = Options.VertexInheritance == EVertexInheritance::None;
		const bool bNearest = Options.VertexInheritance == EVertexInheritance::Nearest;

		// Each output vertex reads from its source vertex, or from the triangle it was placed in.
		auto ForSource = [&](int32 Vid, TFunctionRef<void(const FDynamicMesh3&, int32 /*SrcVid*/, double /*Weight*/)> Visit)
		{
			const FPlanarOutVertex& V = OutVerts[Vid];
			const FDynamicMesh3& M = V.S == 0 ? A : B;
			if (V.SrcVid != INDEX_NONE) { Visit(M, V.SrcVid, 1.0); return; }
			if (V.Home.Tri == INDEX_NONE) { return; }
			const FDynamicMesh3& HM = V.Home.S == 0 ? A : B;
			const FIndex3i T = HM.GetTriangle(V.Home.Tri);
			if (bNearest)
			{
				const FVector3d& W = V.Home.Bary;
				const int32 I = W.X >= W.Y && W.X >= W.Z ? 0 : (W.Y >= W.Z ? 1 : 2);
				Visit(HM, T[I], 1.0);
				return;
			}
			for (int32 K = 0; K < 3; ++K) { if (V.Home.Bary[K] > 0.0) { Visit(HM, T[K], V.Home.Bary[K]); } }
		};

		if (Out.HasVertexNormals() || Out.HasVertexColors() || Out.HasVertexUVs())
		{
			for (int32 Vid = 0; Vid < OutVerts.Num(); ++Vid)
			{
				if (!Out.IsVertex(Vid) || bNone) { continue; }
				FVector3f N(0.0f), Col(0.0f); FVector2f UV(0.0f);
				ForSource(Vid, [&](const FDynamicMesh3& M, int32 S, double W)
				{
					if (M.HasVertexNormals()) { N += M.GetVertexNormal(S) * float(W); }
					if (M.HasVertexColors()) { Col += M.GetVertexColor(S) * float(W); }
					if (M.HasVertexUVs()) { UV += M.GetVertexUV(S) * float(W); }
				});
				if (Out.HasVertexNormals()) { Out.SetVertexNormal(Vid, N.GetSafeNormal()); }
				if (Out.HasVertexColors()) { Out.SetVertexColor(Vid, Col); }
				if (Out.HasVertexUVs()) { Out.SetVertexUV(Vid, UV); }
			}
		}

		if (UE::Geometry::FDynamicMeshAttributeSet* OutAttr = Out.Attributes())
		{
			for (int32 L = 0; L < OutAttr->NumWeightLayers(); ++L)
			{
				UE::Geometry::FDynamicMeshWeightAttribute* Layer = OutAttr->GetWeightLayer(L);
				for (int32 Vid = 0; Vid < OutVerts.Num(); ++Vid)
				{
					if (!Out.IsVertex(Vid)) { continue; }
					float Value = 0.0f;
					if (!bNone)
					{
						ForSource(Vid, [&](const FDynamicMesh3& M, int32 S, double W)
						{
							const UE::Geometry::FDynamicMeshWeightAttribute* Src = M.HasAttributes() && L < M.Attributes()->NumWeightLayers() ? M.Attributes()->GetWeightLayer(L) : nullptr;
							if (Src) { float V = 0.0f; Src->GetValue(S, &V); Value += V * float(W); }
						});
					}
					Layer->SetValue(Vid, &Value);
				}
			}
		}
	}

	void FPlanarBooleanSolver::Assemble()
	{
		using namespace UE::Geometry;
		const FDynamicMesh3& A = *Views[0].Mesh;
		const FDynamicMesh3& B = *Views[1].Mesh;
		const bool bUsesB = Options.OperandMode != EOperandMode::FootprintOnly;
		FDynamicMesh3& Out = Result.Mesh;
		Out = FDynamicMesh3();

		const bool bGroups = A.HasTriangleGroups() || (bUsesB && B.HasTriangleGroups());
		if (bGroups) { Out.EnableTriangleGroups(); }
		if (A.HasVertexNormals() || (bUsesB && B.HasVertexNormals())) { Out.EnableVertexNormals(FVector3f::UnitZ()); }
		if (A.HasVertexColors() || (bUsesB && B.HasVertexColors())) { Out.EnableVertexColors(FVector3f::ZeroVector); }
		if (A.HasVertexUVs() || (bUsesB && B.HasVertexUVs())) { Out.EnableVertexUVs(FVector2f::ZeroVector); }

		const FDynamicMeshAttributeSet* AA = A.Attributes();
		const FDynamicMeshAttributeSet* BA = bUsesB ? B.Attributes() : nullptr;
		if (AA || BA)
		{
			Out.EnableAttributes();
			FDynamicMeshAttributeSet* OA = Out.Attributes();
			if (AA) { OA->EnableMatchingAttributes(*AA, true, false); }
			else { OA->SetNumUVLayers(0); OA->SetNumNormalLayers(0); }
			if (BA)
			{
				if (BA->NumUVLayers() > OA->NumUVLayers()) { OA->SetNumUVLayers(BA->NumUVLayers()); }
				if (BA->NumNormalLayers() > OA->NumNormalLayers()) { OA->SetNumNormalLayers(BA->NumNormalLayers()); }
				if (BA->HasPrimaryColors() && !OA->HasPrimaryColors()) { OA->EnablePrimaryColors(); }
				if (BA->HasMaterialID() && !OA->HasMaterialID()) { OA->EnableMaterialID(); }
				if (BA->NumPolygroupLayers() > OA->NumPolygroupLayers())
				{
					const int32 First = OA->NumPolygroupLayers();
					OA->SetNumPolygroupLayers(BA->NumPolygroupLayers());
					for (int32 L = First; L < BA->NumPolygroupLayers(); ++L) { OA->GetPolygroupLayer(L)->SetName(BA->GetPolygroupLayer(L)->GetName()); }
				}
				if (BA->NumWeightLayers() > OA->NumWeightLayers())
				{
					const int32 First = OA->NumWeightLayers();
					OA->SetNumWeightLayers(BA->NumWeightLayers());
					for (int32 L = First; L < BA->NumWeightLayers(); ++L) { OA->GetWeightLayer(L)->SetName(BA->GetWeightLayer(L)->GetName()); }
				}
			}
			// Attributes this node cannot interpolate: say so, one line each.
			for (const FDynamicMeshAttributeSet* Src : { AA, BA })
			{
				if (!Src) { continue; }
				const FString& Label = Src == AA ? Options.LabelA : Options.LabelB;
				for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& Attached : Src->GetAttachedAttributes())
				{
					Warn(FString::Printf(TEXT("%s: mesh attribute '%s' is not carried through Planar Boolean; its values on the output are defaults."), *Label, *Attached.Key.ToString()));
				}
				for (const TPair<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& Skin : Src->GetSkinWeightsAttributes())
				{
					Warn(FString::Printf(TEXT("%s: skin weight profile '%s' is not carried through Planar Boolean; its values on the output are empty."), *Label, *Skin.Key.ToString()));
				}
				for (const TPair<FName, TUniquePtr<FDynamicMeshMorphTargetAttribute>>& Morph : Src->GetMorphTargetAttributes())
				{
					Warn(FString::Printf(TEXT("%s: morph target '%s' is not carried through Planar Boolean; its values on the output are zero."), *Label, *Morph.Key.ToString()));
				}
			}
		}

		for (const FPlanarOutVertex& V : OutVerts) { Out.AppendVertex(V.P3); }

		const int32 GroupOffset = (bGroups && Options.bOffsetOperandPolygroups) ? A.MaxGroupID() : 0;
		TArray<int32> LayerOffset;
		if (const FDynamicMeshAttributeSet* OA = Out.Attributes())
		{
			LayerOffset.Init(0, OA->NumPolygroupLayers());
			if (AA && Options.bOffsetOperandPolygroups)
			{
				for (int32 L = 0; L < FMath::Min(AA->NumPolygroupLayers(), LayerOffset.Num()); ++L)
				{
					int32 Max = -1;
					for (const int32 Tid : A.TriangleIndicesItr()) { Max = FMath::Max(Max, AA->GetPolygroupLayer(L)->GetValue(Tid)); }
					LayerOffset[L] = Max + 1;
				}
			}
		}

		TArray<int32> RecordTid; RecordTid.Init(INDEX_NONE, Records.Num());
		int32 Failures = 0;
		for (int32 RI = 0; RI < Records.Num(); ++RI)
		{
			const FPlanarRecord& R = Records[RI];
			const FDynamicMesh3& M = R.S == 0 ? A : B;
			int32 Group = 0;
			if (bGroups && M.HasTriangleGroups() && R.SrcTri >= 0) { Group = M.GetTriangleGroup(R.SrcTri) + (R.S == 1 ? GroupOffset : 0); }
			const int32 Tid = Out.AppendTriangle(FIndex3i(R.V[0], R.V[1], R.V[2]), Group);
			if (Tid < 0)
			{
				++Failures;
				if (Failures <= 10)
				{
					Warn(FString::Printf(TEXT("Output triangle (%d, %d, %d) from %s triangle %d could not be added: %s."),
						R.V[0], R.V[1], R.V[2], R.S == 0 ? *Options.LabelA : *Options.LabelB, R.SrcTri,
						Tid == FDynamicMesh3::NonManifoldID ? TEXT("one of its edges would be shared by three triangles") : TEXT("it duplicates another output triangle")));
				}
				continue;
			}
			RecordTid[RI] = Tid;
		}
		if (Failures > 10) { Warn(FString::Printf(TEXT("%d more output triangles could not be added and are not listed."), Failures - 10)); }

		if (FDynamicMeshAttributeSet* OA = Out.Attributes())
		{
			if (FDynamicMeshMaterialAttribute* OutMat = OA->GetMaterialID())
			{
				for (int32 RI = 0; RI < Records.Num(); ++RI)
				{
					const int32 Tid = RecordTid[RI];
					if (Tid < 0) { continue; }
					const FPlanarRecord& R = Records[RI];
					const FDynamicMeshAttributeSet* SA = R.S == 0 ? AA : BA;
					int32 Value = (SA && SA->GetMaterialID() && R.SrcTri >= 0) ? SA->GetMaterialID()->GetValue(R.SrcTri) : 0;
					if (R.S == 1 && Options.OperandMaterialRemap.IsValidIndex(Value)) { Value = Options.OperandMaterialRemap[Value]; }
					OutMat->SetValue(Tid, Value);
				}
			}
			for (int32 L = 0; L < OA->NumPolygroupLayers(); ++L)
			{
				FDynamicMeshPolygroupAttribute* OutLayer = OA->GetPolygroupLayer(L);
				for (int32 RI = 0; RI < Records.Num(); ++RI)
				{
					const int32 Tid = RecordTid[RI];
					if (Tid < 0) { continue; }
					const FPlanarRecord& R = Records[RI];
					const FDynamicMeshAttributeSet* SA = R.S == 0 ? AA : BA;
					int32 Value = 0;
					if (SA && L < SA->NumPolygroupLayers() && R.SrcTri >= 0) { Value = SA->GetPolygroupLayer(L)->GetValue(R.SrcTri) + (R.S == 1 ? LayerOffset[L] : 0); }
					OutLayer->SetValue(Tid, Value);
				}
			}

			TArray<FVector3f> VertexNormals;
			if (OA->NumNormalLayers() > 0)
			{
				FMeshNormals Normals(&Out);
				Normals.ComputeVertexNormals();
				VertexNormals.SetNum(Out.MaxVertexID());
				for (const int32 Vid : Out.VertexIndicesItr()) { VertexNormals[Vid] = FVector3f(Normals[Vid]); }
			}
			const bool bFlipB = Views[0].bHasArea && Views[1].bHasArea && Views[0].Sign != Views[1].Sign;
			for (int32 L = 0; L < OA->NumUVLayers(); ++L)
			{
				PlanarTransferOverlay(OA->GetUVLayer(L), AA ? AA->GetUVLayer(L) : nullptr, BA ? BA->GetUVLayer(L) : nullptr,
					Out, Records, RecordTid, Options.VertexInheritance, false, false, VertexNormals);
			}
			for (int32 L = 0; L < OA->NumNormalLayers(); ++L)
			{
				PlanarTransferOverlay(OA->GetNormalLayer(L), AA ? AA->GetNormalLayer(L) : nullptr, BA ? BA->GetNormalLayer(L) : nullptr,
					Out, Records, RecordTid, Options.VertexInheritance, true, bFlipB, VertexNormals);
			}
			if (OA->HasPrimaryColors())
			{
				PlanarTransferOverlay(OA->PrimaryColors(), AA ? AA->PrimaryColors() : nullptr, BA ? BA->PrimaryColors() : nullptr,
					Out, Records, RecordTid, Options.VertexInheritance, false, false, VertexNormals);
			}
		}
		TransferVertexAttributes(Out);

		// Welded operand vertices and points the triangulation dropped are left without triangles: remove them.
		bool bRemoved = false;
		for (int32 Vid = 0; Vid < Out.MaxVertexID(); ++Vid)
		{
			if (Out.IsVertex(Vid) && Out.GetVtxEdgeCount(Vid) == 0)
			{
				Out.RemoveVertex(Vid);
				bRemoved = true;
			}
		}
		if (bRemoved) { Out.CompactInPlace(); }
	}

	bool FPlanarBooleanSolver::Run()
	{
		InitView(Views[0]);
		InitView(Views[1]);

		const bool bStitch = Options.OperandMode == EOperandMode::Whole;
		if (Options.bCheckCoplanar)
		{
			CheckCoplanar(Views[0]);
			CheckCoplanar(Views[1]);
		}
		if (Options.bCheckFolds)
		{
			if (Options.bCheckCoplanar) { CheckFolds(Views[0], nullptr); }
			else { const FPlanarBox Restrict = Views[1].Bounds.Grown(Snap); CheckFolds(Views[0], &Restrict); }
			CheckFolds(Views[1], nullptr);
		}
		BuildBoundary(Views[0]);
		BuildBoundary(Views[1]);
		if (bStitch) { CheckStitchOperandOutline(); }
		if (bFatal) { return false; }

		FindCut(Views[0], Views[1]);
		if (Options.OperandMode == EOperandMode::Cut && CanOwn(1)) { FindCut(Views[1], Views[0]); }
		Result.CutTrianglesA = Views[0].CutTriangles.Num();
		Result.CutTrianglesB = Views[1].CutTriangles.Num();

		int32 Emitted[2] = { 0, 0 };
		for (int32 SIndex = 0; SIndex < 2; ++SIndex)
		{
			FPlanarView& S = Views[SIndex];
			const FPlanarView& O = Views[1 - SIndex];
			if (SIndex == 1 && Options.OperandMode == EOperandMode::FootprintOnly) { continue; }
			const int32 Before = Records.Num();
			const bool bWholeOnly = SIndex == 1 && bStitch;
			if (!bWholeOnly && !CanOwn(SIndex)) { continue; }
			for (const int32 Tid : S.Mesh->TriangleIndicesItr())
			{
				if (S.State[Tid] == ETriState::None) { continue; }
				if (bWholeOnly) { EmitWhole(S, Tid); continue; }
				if (S.Cut[Tid]) { continue; }
				const bool bInO = PlanarInside(O, S.Centroid(Tid));
				if (PlanarOwner(Options, SIndex == 0 ? true : bInO, SIndex == 0 ? bInO : true) == SIndex) { EmitWhole(S, Tid); }
			}
			if (!bWholeOnly) { BuildPieces(S, O); }
			Emitted[SIndex] = Records.Num() - Before;
		}

		if (Emitted[0] > 0 && Emitted[1] > 0)
		{
			RepairSeams();
			Weld();
			if (bStitch) { BuildWalls(); }
		}
		Assemble();
		return true;
	}
}

bool FPlanarFrame::FromNormal(const FVector3d& InOrigin, const FVector3d& InNormal, FPlanarFrame& OutFrame)
{
	const FVector3d N = InNormal.GetSafeNormal();
	if (N.IsNearlyZero()) { return false; }
	// Stable axes: X follows world X projected into the plane unless the normal is close to X.
	const FVector3d Seed = FMath::Abs(N.X) < 0.9 ? FVector3d::UnitX() : FVector3d::UnitY();
	OutFrame.Origin = InOrigin;
	OutFrame.Normal = N;
	OutFrame.AxisX = (Seed - N * Seed.Dot(N)).GetSafeNormal();
	OutFrame.AxisY = N.Cross(OutFrame.AxisX).GetSafeNormal();
	return true;
}

bool FResult::HasErrors() const
{
	return Messages.ContainsByPredicate([](const FMessage& M) { return M.bError; });
}

bool AreaCentroid(const UE::Geometry::FDynamicMesh3& Mesh, FVector3d& OutCentroid)
{
	double Area = 0.0;
	FVector3d Sum = FVector3d::ZeroVector;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Mesh.GetTriVertices(Tid, A, B, C);
		const double TA = (B - A).Cross(C - A).Length() * 0.5;
		Area += TA;
		Sum += (A + B + C) / 3.0 * TA;
	}
	if (Area <= 0.0) { return false; }
	OutCentroid = Sum / Area;
	return true;
}

bool FitPlane(const UE::Geometry::FDynamicMesh3& Mesh, FPlanarFrame& OutFrame)
{
	FVector3d Centroid;
	if (!AreaCentroid(Mesh, Centroid)) { return false; }
	FVector3d N = FVector3d::ZeroVector;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		N += Mesh.GetTriNormal(Tid) * Mesh.GetTriArea(Tid);
	}
	return FPlanarFrame::FromNormal(Centroid, N, OutFrame);
}

double ProjectedArea(const UE::Geometry::FDynamicMesh3& Mesh, const FPlanarFrame& Frame)
{
	double Sum = 0.0;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Mesh.GetTriVertices(Tid, A, B, C);
		const FVector2d PA = Frame.To2D(A), PB = Frame.To2D(B), PC = Frame.To2D(C);
		Sum += FMath::Abs(PlanarBooleanPrivate::PlanarCross(PB - PA, PC - PA)) * 0.5;
	}
	return Sum;
}

bool Compute(const UE::Geometry::FDynamicMesh3& A, const UE::Geometry::FDynamicMesh3& B, const FPlanarFrame& Frame,
	const FOptions& Options, FResult& OutResult)
{
	OutResult.Mesh = UE::Geometry::FDynamicMesh3();
	OutResult.Messages.Reset();
	OutResult.CutTrianglesA = OutResult.CutTrianglesB = OutResult.SeamWelds = OutResult.SeamGapVertices = OutResult.WallTriangles = 0;
	PlanarBooleanPrivate::FPlanarBooleanSolver Solver(A, B, Frame, Options, OutResult);
	const bool bOK = Solver.Run();
	if (!bOK) { OutResult.Mesh = UE::Geometry::FDynamicMesh3(); }
	return bOK;
}
}
