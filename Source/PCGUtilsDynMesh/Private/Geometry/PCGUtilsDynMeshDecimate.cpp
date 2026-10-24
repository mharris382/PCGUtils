// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshDecimate.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"

namespace PCGUtilsDynMeshDecimateImpl
{
	using namespace UE::Geometry;

	constexpr double HalfPi = UE_DOUBLE_HALF_PI;

	/** Angle between two directions in radians; 0 when either is zero length. */
	double AngleBetween(const FVector3d& A, const FVector3d& B)
	{
		const double LengthProduct = A.Length() * B.Length();
		if (LengthProduct <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 0.0;
		}
		return FMath::Acos(FMath::Clamp(A.Dot(B) / LengthProduct, -1.0, 1.0));
	}

	/** Twice the triangle's area vector (unnormalized normal). */
	FVector3d AreaVector(const FVector3d& A, const FVector3d& B, const FVector3d& C)
	{
		return (B - A).Cross(C - A);
	}

	FVector3d TriangleAreaVector(const FDynamicMesh3& Mesh, int32 TriangleID)
	{
		FVector3d A, B, C;
		Mesh.GetTriVertices(TriangleID, A, B, C);
		return AreaVector(A, B, C);
	}

	/** True when merging across this edge is forbidden by a delimit option. */
	bool IsDelimited(const FDynamicMesh3& Mesh, int32 EdgeID, const PCGUtilsDynMeshDecimate::FPlanarOptions& Options)
	{
		const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
		if (Tris.B == FDynamicMesh3::InvalidID)
		{
			return true;
		}
		if (Options.bDelimitPolygroup && Mesh.HasTriangleGroups() &&
			Mesh.GetTriangleGroup(Tris.A) != Mesh.GetTriangleGroup(Tris.B))
		{
			return true;
		}
		const FDynamicMeshAttributeSet* Attributes = Mesh.HasAttributes() ? Mesh.Attributes() : nullptr;
		if (!Attributes)
		{
			return false;
		}
		if (Options.bDelimitMaterial && Attributes->HasMaterialID() &&
			Attributes->GetMaterialID()->GetValue(Tris.A) != Attributes->GetMaterialID()->GetValue(Tris.B))
		{
			return true;
		}
		if (Options.bDelimitUVSeams)
		{
			for (int32 Layer = 0; Layer < Attributes->NumUVLayers(); ++Layer)
			{
				if (Attributes->GetUVLayer(Layer)->IsSeamEdge(EdgeID))
				{
					return true;
				}
			}
		}
		if (Options.bDelimitNormalSeams && Attributes->PrimaryNormals() && Attributes->PrimaryNormals()->IsSeamEdge(EdgeID))
		{
			return true;
		}
		return false;
	}

	/**
	 * Given which triangles form each region, removes vertices by edge collapse and tidies each region's
	 * triangulation with edge flips. Triangles outside the selection are locked: never changed, and no vertex
	 * touching one is removed.
	 */
	class FRegionDissolver
	{
	public:
		FRegionDissolver(FDynamicMesh3& InMesh, const TSet<int32>* Selected)
			: Mesh(InMesh)
		{
			RegionOf.Init(INDEX_NONE, Mesh.MaxTriangleID());
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				if (!Selected || Selected->Contains(TriangleID))
				{
					RegionOf[TriangleID] = TriangleID;
				}
			}
			const FAxisAlignedBox3d Bounds = Mesh.GetBounds(true);
			MinArea = FMath::Max(1e-12 * Bounds.DiagonalLength() * Bounds.DiagonalLength(), 1e-14);
		}

		FDynamicMesh3& Mesh;
		/** Region per triangle ID; INDEX_NONE for a locked (unselected) triangle. */
		TArray<int32> RegionOf;

		/** Call once RegionOf is final and before removing vertices. */
		void FinalizeRegions()
		{
			RegionTriCount.Init(0, RegionOf.Num());
			RegionNormal.Init(FVector3d::ZeroVector, RegionOf.Num());
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				const int32 Region = RegionOf[TriangleID];
				if (Region != INDEX_NONE)
				{
					++RegionTriCount[Region];
					RegionNormal[Region] += TriangleAreaVector(Mesh, TriangleID);
				}
			}
			for (FVector3d& Normal : RegionNormal)
			{
				Normal = Normalized(Normal);
			}
		}

		int32 CountRegions() const
		{
			int32 Count = 0;
			for (const int32 Tris : RegionTriCount)
			{
				Count += Tris > 0;
			}
			return Count;
		}

		bool IsRegionBoundaryEdge(int32 EdgeID) const
		{
			const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
			return Tris.B == FDynamicMesh3::InvalidID || RegionOf[Tris.A] == INDEX_NONE || RegionOf[Tris.A] != RegionOf[Tris.B];
		}

		bool TouchesLocked(int32 VertexID) const
		{
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID))
			{
				if (RegionOf[TriangleID] == INDEX_NONE)
				{
					return true;
				}
			}
			return false;
		}

		/** The region a vertex lies strictly inside of, or INDEX_NONE. */
		int32 InteriorRegion(int32 VertexID) const
		{
			if (Mesh.IsBoundaryVertex(VertexID))
			{
				return INDEX_NONE;
			}
			int32 Region = INDEX_NONE;
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID))
			{
				const int32 TriRegion = RegionOf[TriangleID];
				if (TriRegion == INDEX_NONE || (Region != INDEX_NONE && TriRegion != Region))
				{
					return INDEX_NONE;
				}
				Region = TriRegion;
			}
			return Region;
		}

		/** A vertex on exactly two region-outline edges; outputs the vertices at their far ends. */
		bool GetChain(int32 VertexID, int32& OutA, int32& OutB) const
		{
			if (TouchesLocked(VertexID))
			{
				return false;
			}
			int32 Found = 0;
			for (const int32 EdgeID : Mesh.VtxEdgesItr(VertexID))
			{
				if (IsRegionBoundaryEdge(EdgeID))
				{
					const FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
					const int32 Other = Verts.A == VertexID ? Verts.B : Verts.A;
					if (Found == 0) { OutA = Other; }
					else if (Found == 1) { OutB = Other; }
					if (++Found > 2)
					{
						return false;
					}
				}
			}
			return Found == 2;
		}

		/** Collapses Remove into Keep when no triangle would flip, degenerate, or leave its region empty. */
		bool TryCollapse(int32 Remove, int32 Keep)
		{
			const int32 EdgeID = Mesh.FindEdge(Remove, Keep);
			if (EdgeID == FDynamicMesh3::InvalidID)
			{
				return false;
			}
			const FIndex2i EdgeTris = Mesh.GetEdgeT(EdgeID);
			for (const int32 TriangleID : {EdgeTris.A, EdgeTris.B})
			{
				if (TriangleID != FDynamicMesh3::InvalidID && RegionTriCount[RegionOf[TriangleID]] <= 1)
				{
					return false;
				}
			}

			const FVector3d KeepPosition = Mesh.GetVertex(Keep);
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(Remove))
			{
				if (TriangleID == EdgeTris.A || TriangleID == EdgeTris.B)
				{
					continue;
				}
				const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
				FVector3d P[3] = {Mesh.GetVertex(Tri.A), Mesh.GetVertex(Tri.B), Mesh.GetVertex(Tri.C)};
				const FVector3d Before = AreaVector(P[0], P[1], P[2]);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (Tri[Corner] == Remove)
					{
						P[Corner] = KeepPosition;
					}
				}
				const FVector3d After = AreaVector(P[0], P[1], P[2]);
				if (After.Length() * 0.5 <= MinArea || After.Dot(Before) <= 0.0)
				{
					return false;
				}
				const int32 Region = RegionOf[TriangleID];
				if (Region != INDEX_NONE && After.Dot(RegionNormal[Region]) <= 0.0)
				{
					return false;
				}
			}

			if (Mesh.CanCollapseEdge(Keep, Remove, 0.0) != EMeshResult::Ok)
			{
				return false;
			}
			FDynamicMesh3::FEdgeCollapseInfo Info;
			if (Mesh.CollapseEdge(Keep, Remove, 0.0, Info) != EMeshResult::Ok)
			{
				return false;
			}
			for (const int32 TriangleID : {Info.RemovedTris.A, Info.RemovedTris.B})
			{
				if (TriangleID != FDynamicMesh3::InvalidID)
				{
					--RegionTriCount[RegionOf[TriangleID]];
				}
			}
			return true;
		}

		/** Removes every vertex strictly inside a region. Returns how many could not be removed. */
		int32 RemoveInteriorVertices(int32& InOutRemoved)
		{
			TArray<int32> Pending;
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				if (InteriorRegion(VertexID) != INDEX_NONE)
				{
					Pending.Add(VertexID);
				}
			}
			// A collapse that fails now (a concave outline, say) can succeed once neighbors are gone.
			for (int32 Pass = 0; Pass < 8 && !Pending.IsEmpty(); ++Pass)
			{
				TArray<int32> Failed;
				for (const int32 VertexID : Pending)
				{
					if (!Mesh.IsVertex(VertexID) || InteriorRegion(VertexID) == INDEX_NONE)
					{
						continue;
					}
					if (CollapseIntoNearestNeighbor(VertexID))
					{
						++InOutRemoved;
					}
					else
					{
						Failed.Add(VertexID);
					}
				}
				if (Failed.Num() == Pending.Num())
				{
					break;
				}
				Pending = MoveTemp(Failed);
			}
			int32 Blocked = 0;
			for (const int32 VertexID : Pending)
			{
				Blocked += Mesh.IsVertex(VertexID) && InteriorRegion(VertexID) != INDEX_NONE;
			}
			return Blocked;
		}

		/**
		 * Removes outline vertices, cheapest first. Cost(Vertex, A, B) returns the vertex's cost, or a negative value
		 * to keep it. Each vertex collapses into one of its two outline neighbors, so the outline keeps its shape.
		 */
		int32 RemoveChainVertices(const TSet<int32>& Candidates,
			TFunctionRef<double(int32 Vertex, int32 A, int32 B)> Cost, int32& InOutRemoved)
		{
			struct FEntry
			{
				double Cost;
				int32 Vertex;
				bool operator<(const FEntry& Other) const { return Cost < Other.Cost || (Cost == Other.Cost && Vertex < Other.Vertex); }
			};
			TArray<FEntry> Heap;
			auto PushVertex = [&](int32 VertexID)
			{
				int32 A, B;
				if (Candidates.Contains(VertexID) && Mesh.IsVertex(VertexID) && GetChain(VertexID, A, B))
				{
					const double VertexCost = Cost(VertexID, A, B);
					if (VertexCost >= 0.0)
					{
						Heap.HeapPush({VertexCost, VertexID});
					}
				}
			};
			for (const int32 VertexID : Candidates)
			{
				PushVertex(VertexID);
			}

			TSet<int32> Blocked;
			while (!Heap.IsEmpty())
			{
				FEntry Entry;
				Heap.HeapPop(Entry);
				int32 A, B;
				if (!Mesh.IsVertex(Entry.Vertex) || !GetChain(Entry.Vertex, A, B))
				{
					continue;
				}
				const double Current = Cost(Entry.Vertex, A, B);
				if (Current < 0.0)
				{
					continue;
				}
				if (!FMath::IsNearlyEqual(Current, Entry.Cost, 1e-12))
				{
					Heap.HeapPush({Current, Entry.Vertex});
					continue;
				}
				const bool bANearer = DistanceSquared(Mesh.GetVertex(Entry.Vertex), Mesh.GetVertex(A)) <=
					DistanceSquared(Mesh.GetVertex(Entry.Vertex), Mesh.GetVertex(B));
				const int32 First = bANearer ? A : B;
				const int32 Second = bANearer ? B : A;
				auto Collapse = [&]() { return TryCollapse(Entry.Vertex, First) || TryCollapse(Entry.Vertex, Second); };
				if (Collapse() || (ReduceValence(Entry.Vertex) && Collapse()))
				{
					++InOutRemoved;
					Blocked.Remove(Entry.Vertex);
					PushVertex(A);
					PushVertex(B);
				}
				else
				{
					Blocked.Add(Entry.Vertex);
				}
			}
			int32 StillBlocked = 0;
			for (const int32 VertexID : Blocked)
			{
				StillBlocked += Mesh.IsVertex(VertexID);
			}
			return StillBlocked;
		}

		/**
		 * Flips edges inside regions toward a Delaunay triangulation: an edge flips when the two angles facing
		 * it sum to more than 180 degrees. Only edges whose two triangles bend by at most MaxDihedral flip, so
		 * a curved region keeps its shape.
		 */
		void ImproveTriangulation(double MaxDihedralRadians)
		{
			const bool bHasAttributes = Mesh.HasAttributes();
			for (int32 Pass = 0; Pass < 64; ++Pass)
			{
				bool bChanged = false;
				for (const int32 EdgeID : Mesh.EdgeIndicesItr())
				{
					const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
					if (Tris.B == FDynamicMesh3::InvalidID || RegionOf[Tris.A] == INDEX_NONE || RegionOf[Tris.A] != RegionOf[Tris.B])
					{
						continue;
					}
					if (bHasAttributes && Mesh.Attributes()->IsSeamEdge(EdgeID))
					{
						continue;
					}
					const FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
					const FIndex2i Opposing = Mesh.GetEdgeOpposingV(EdgeID);
					if (Mesh.FindEdge(Opposing.A, Opposing.B) != FDynamicMesh3::InvalidID)
					{
						continue;
					}
					const FVector3d NormalA = TriangleAreaVector(Mesh, Tris.A);
					const FVector3d NormalB = TriangleAreaVector(Mesh, Tris.B);
					if (AngleBetween(NormalA, NormalB) > MaxDihedralRadians)
					{
						continue;
					}
					const FVector3d A = Mesh.GetVertex(Verts.A), B = Mesh.GetVertex(Verts.B);
					const FVector3d C = Mesh.GetVertex(Opposing.A), D = Mesh.GetVertex(Opposing.B);
					if (AngleBetween(A - C, B - C) + AngleBetween(A - D, B - D) <= UE_DOUBLE_PI + 1e-9)
					{
						continue;
					}
					const FVector3d Reference = NormalA + NormalB;
					FDynamicMesh3::FEdgeFlipInfo Info;
					if (Mesh.FlipEdge(EdgeID, Info) != EMeshResult::Ok)
					{
						continue;
					}
					const FVector3d NewA = TriangleAreaVector(Mesh, Info.Triangles.A);
					const FVector3d NewB = TriangleAreaVector(Mesh, Info.Triangles.B);
					if (NewA.Dot(Reference) <= 0.0 || NewB.Dot(Reference) <= 0.0 ||
						NewA.Length() * 0.5 <= MinArea || NewB.Length() * 0.5 <= MinArea)
					{
						FDynamicMesh3::FEdgeFlipInfo Undo;
						Mesh.FlipEdge(Info.EdgeID, Undo);
						continue;
					}
					bChanged = true;
				}
				if (!bChanged)
				{
					break;
				}
			}
		}

		TArray<FVector3d> RegionNormal;

	private:
		/** Flips Tris' shared edge if both new triangles stay non-degenerate and face their old direction. */
		bool TryFlip(int32 EdgeID)
		{
			const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
			if (Tris.B == FDynamicMesh3::InvalidID || RegionOf[Tris.A] == INDEX_NONE || RegionOf[Tris.A] != RegionOf[Tris.B] ||
				(Mesh.HasAttributes() && Mesh.Attributes()->IsSeamEdge(EdgeID)))
			{
				return false;
			}
			const FIndex2i Opposing = Mesh.GetEdgeOpposingV(EdgeID);
			if (Mesh.FindEdge(Opposing.A, Opposing.B) != FDynamicMesh3::InvalidID)
			{
				return false;
			}
			const FVector3d Reference = TriangleAreaVector(Mesh, Tris.A) + TriangleAreaVector(Mesh, Tris.B);
			FDynamicMesh3::FEdgeFlipInfo Info;
			if (Mesh.FlipEdge(EdgeID, Info) != EMeshResult::Ok)
			{
				return false;
			}
			const FVector3d NewA = TriangleAreaVector(Mesh, Info.Triangles.A);
			const FVector3d NewB = TriangleAreaVector(Mesh, Info.Triangles.B);
			if (NewA.Dot(Reference) <= 0.0 || NewB.Dot(Reference) <= 0.0 ||
				NewA.Length() * 0.5 <= MinArea || NewB.Length() * 0.5 <= MinArea)
			{
				FDynamicMesh3::FEdgeFlipInfo Undo;
				Mesh.FlipEdge(Info.EdgeID, Undo);
				return false;
			}
			return true;
		}

	public:
		/**
		 * Flips edges inside regions away from a vertex so fewer triangles meet at it. A vertex on a straight outline
		 * often cannot collapse while its fan reaches other vertices on that same line; with fewer triangles it can.
		 * Returns whether anything flipped.
		 */
		bool ReduceValence(int32 VertexID)
		{
			bool bAny = false;
			for (int32 Pass = 0; Pass < 16; ++Pass)
			{
				TArray<int32, TInlineAllocator<16>> Edges;
				for (const int32 EdgeID : Mesh.VtxEdgesItr(VertexID))
				{
					Edges.Add(EdgeID);
				}
				bool bFlipped = false;
				for (const int32 EdgeID : Edges)
				{
					if (Mesh.IsEdge(EdgeID) && Mesh.GetEdgeV(EdgeID).Contains(VertexID) && TryFlip(EdgeID))
					{
						bFlipped = true;
						break;
					}
				}
				if (!bFlipped)
				{
					break;
				}
				bAny = true;
			}
			return bAny;
		}

	private:
		bool CollapseIntoNearestNeighbor(int32 VertexID)
		{
			if (CollapseIntoNearestNeighborOnce(VertexID))
			{
				return true;
			}
			return ReduceValence(VertexID) && CollapseIntoNearestNeighborOnce(VertexID);
		}

		bool CollapseIntoNearestNeighborOnce(int32 VertexID)
		{
			TArray<TPair<double, int32>, TInlineAllocator<16>> Neighbors;
			const FVector3d Position = Mesh.GetVertex(VertexID);
			for (const int32 Neighbor : Mesh.VtxVerticesItr(VertexID))
			{
				Neighbors.Add({DistanceSquared(Position, Mesh.GetVertex(Neighbor)), Neighbor});
			}
			Neighbors.Sort([](const TPair<double, int32>& L, const TPair<double, int32>& R) { return L.Key < R.Key; });
			for (const TPair<double, int32>& Neighbor : Neighbors)
			{
				if (TryCollapse(VertexID, Neighbor.Value))
				{
					return true;
				}
			}
			return false;
		}

		TArray<int32> RegionTriCount;
		double MinArea = 1e-14;
	};

	/** Greedy merge of triangles into regions whose normals stay within the angle limit, each a topological disk. */
	class FPlanarRegionBuilder
	{
	public:
		FPlanarRegionBuilder(const FDynamicMesh3& InMesh, const TArray<int32>& InitialRegions,
			const PCGUtilsDynMeshDecimate::FPlanarOptions& InOptions)
			: Mesh(InMesh), Options(InOptions)
		{
			const int32 Max = Mesh.MaxTriangleID();
			Parent.Init(INDEX_NONE, Max);
			Area.Init(FVector3d::ZeroVector, Max);
			Boundary.SetNum(Max);
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				if (InitialRegions[TriangleID] == INDEX_NONE)
				{
					continue;
				}
				Parent[TriangleID] = TriangleID;
				Area[TriangleID] = TriangleAreaVector(Mesh, TriangleID);
				const FIndex3i Edges = Mesh.GetTriEdges(TriangleID);
				Boundary[TriangleID] = {Edges.A, Edges.B, Edges.C};
			}
			AngleLimit = FMath::DegreesToRadians(FMath::Max(Options.AngleLimitDegrees, 0.0));
		}

		/** Runs the merge and writes each triangle's region (its root triangle) into InOutRegions. */
		void Build(TArray<int32>& InOutRegions)
		{
			for (const int32 EdgeID : Mesh.EdgeIndicesItr())
			{
				PushEdge(EdgeID);
			}
			while (!Heap.IsEmpty())
			{
				FEntry Entry;
				Heap.HeapPop(Entry);
				const FIndex2i Tris = Mesh.GetEdgeT(Entry.Edge);
				const int32 RootA = Find(Tris.A);
				const int32 RootB = Find(Tris.B);
				if (RootA == RootB)
				{
					continue;
				}
				const double Cost = AngleBetween(Area[RootA], Area[RootB]);
				if (Cost > AngleLimit)
				{
					continue;
				}
				if (!FMath::IsNearlyEqual(Cost, Entry.Cost, 1e-12))
				{
					Heap.HeapPush({Cost, Entry.Edge});
					continue;
				}
				TryMerge(RootA, RootB);
			}
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				if (Parent[TriangleID] != INDEX_NONE)
				{
					InOutRegions[TriangleID] = Find(TriangleID);
				}
			}
		}

	private:
		struct FEntry
		{
			double Cost;
			int32 Edge;
			bool operator<(const FEntry& Other) const { return Cost < Other.Cost || (Cost == Other.Cost && Edge < Other.Edge); }
		};

		int32 Find(int32 TriangleID)
		{
			int32 Root = TriangleID;
			while (Parent[Root] != Root)
			{
				Root = Parent[Root];
			}
			while (Parent[TriangleID] != Root)
			{
				const int32 Next = Parent[TriangleID];
				Parent[TriangleID] = Root;
				TriangleID = Next;
			}
			return Root;
		}

		void PushEdge(int32 EdgeID)
		{
			const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
			if (Tris.B == FDynamicMesh3::InvalidID || Parent[Tris.A] == INDEX_NONE || Parent[Tris.B] == INDEX_NONE ||
				IsDelimited(Mesh, EdgeID, Options))
			{
				return;
			}
			const int32 RootA = Find(Tris.A);
			const int32 RootB = Find(Tris.B);
			if (RootA == RootB)
			{
				return;
			}
			const double Cost = AngleBetween(Area[RootA], Area[RootB]);
			if (Cost <= AngleLimit)
			{
				Heap.HeapPush({Cost, EdgeID});
			}
		}

		/** Merges two regions if their union is still a disk: they share one open run of edges and touch nowhere else. */
		bool TryMerge(int32 RootA, int32 RootB)
		{
			if (Boundary[RootA].Num() > Boundary[RootB].Num())
			{
				Swap(RootA, RootB);
			}

			// Shared edges, from the smaller region's side.
			TArray<int32, TInlineAllocator<16>> Shared;
			for (const int32 EdgeID : Boundary[RootA])
			{
				const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
				if (Tris.B != FDynamicMesh3::InvalidID &&
					((Parent[Tris.A] != INDEX_NONE && Find(Tris.A) == RootB) || (Parent[Tris.B] != INDEX_NONE && Find(Tris.B) == RootB)))
				{
					Shared.Add(EdgeID);
				}
			}
			if (Shared.IsEmpty())
			{
				return false;
			}

			// One open path: exactly two ends, and walking from one end visits every shared edge.
			TMap<int32, TArray<int32, TInlineAllocator<2>>> VertexEdges;
			for (const int32 EdgeID : Shared)
			{
				const FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
				VertexEdges.FindOrAdd(Verts.A).Add(EdgeID);
				VertexEdges.FindOrAdd(Verts.B).Add(EdgeID);
			}
			int32 Start = INDEX_NONE;
			int32 Ends = 0;
			for (const TPair<int32, TArray<int32, TInlineAllocator<2>>>& Pair : VertexEdges)
			{
				if (Pair.Value.Num() > 2)
				{
					return false;
				}
				if (Pair.Value.Num() == 1)
				{
					++Ends;
					Start = Pair.Key;
				}
			}
			if (Ends != 2)
			{
				return false;
			}
			int32 Visited = 0;
			int32 Vertex = Start;
			int32 PreviousEdge = INDEX_NONE;
			while (true)
			{
				const TArray<int32, TInlineAllocator<2>>& Edges = VertexEdges[Vertex];
				const int32 NextEdge = Edges[0] != PreviousEdge ? Edges[0] : (Edges.Num() > 1 ? Edges[1] : INDEX_NONE);
				if (NextEdge == INDEX_NONE || NextEdge == PreviousEdge)
				{
					break;
				}
				++Visited;
				const FIndex2i Verts = Mesh.GetEdgeV(NextEdge);
				Vertex = Verts.A == Vertex ? Verts.B : Verts.A;
				PreviousEdge = NextEdge;
				if (VertexEdges[Vertex].Num() == 1)
				{
					break;
				}
			}
			if (Visited != Shared.Num())
			{
				return false;
			}

			// No other contact: an outline vertex of A off the path must not be on B's outline.
			for (const int32 EdgeID : Boundary[RootA])
			{
				const FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
				for (const int32 OutlineVertex : {Verts.A, Verts.B})
				{
					if (VertexEdges.Contains(OutlineVertex))
					{
						continue;
					}
					for (const int32 Incident : Mesh.VtxEdgesItr(OutlineVertex))
					{
						if (Boundary[RootB].Contains(Incident))
						{
							return false;
						}
					}
				}
			}

			// Merge A into B.
			const FVector3d OldNormal = Area[RootB];
			Parent[RootA] = RootB;
			Area[RootB] += Area[RootA];
			TArray<int32> Exposed;
			for (const int32 EdgeID : Boundary[RootA])
			{
				if (Shared.Contains(EdgeID))
				{
					Boundary[RootB].Remove(EdgeID);
				}
				else
				{
					Boundary[RootB].Add(EdgeID);
					Exposed.Add(EdgeID);
				}
			}
			Boundary[RootA].Empty();

			// The merged normal moved: every outline edge's cost changed. Otherwise only A's old outline is new.
			if (AngleBetween(OldNormal, Area[RootB]) > 1e-9)
			{
				for (const int32 EdgeID : Boundary[RootB])
				{
					PushEdge(EdgeID);
				}
			}
			else
			{
				for (const int32 EdgeID : Exposed)
				{
					PushEdge(EdgeID);
				}
			}
			return true;
		}

		const FDynamicMesh3& Mesh;
		const PCGUtilsDynMeshDecimate::FPlanarOptions& Options;
		double AngleLimit = 0.0;
		TArray<int32> Parent;
		TArray<FVector3d> Area;
		TArray<TSet<int32>> Boundary;
		TArray<FEntry> Heap;
	};

	/** Quad pairs of triangles found in the mesh, plus the subdivision class of each vertex. */
	struct FQuadGrid
	{
		struct FQuad
		{
			FIndex4i Verts; // cyclic, in the triangles' winding
			FIndex2i Tris;
		};
		TArray<FQuad> Quads;
		TArray<TArray<int32, TInlineAllocator<4>>> VertexQuads;
		TArray<int32> QuadOfTriangle;
		/** ClassOriginal, ClassEdge or ClassFace, or INDEX_NONE when unlabeled. */
		TArray<int32> Label;
		TArray<bool> Conflict;
	};

	/** Pairs triangles into the best-shaped quads available: near right angles, convex, nearly flat, same material and group. */
	void PairQuads(const FDynamicMesh3& Mesh, const TArray<int32>& Unlocked, FQuadGrid& Grid)
	{
		struct FCandidate
		{
			double Score;
			int32 Edge;
			FIndex4i Verts;
		};
		TArray<FCandidate> Candidates;
		const FDynamicMeshMaterialAttribute* Materials = Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID()
			? Mesh.Attributes()->GetMaterialID() : nullptr;

		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
			if (Tris.B == FDynamicMesh3::InvalidID || Unlocked[Tris.A] == INDEX_NONE || Unlocked[Tris.B] == INDEX_NONE)
			{
				continue;
			}
			if ((Materials && Materials->GetValue(Tris.A) != Materials->GetValue(Tris.B)) ||
				(Mesh.HasTriangleGroups() && Mesh.GetTriangleGroup(Tris.A) != Mesh.GetTriangleGroup(Tris.B)))
			{
				continue;
			}
			const FVector3d NormalA = TriangleAreaVector(Mesh, Tris.A);
			const FVector3d NormalB = TriangleAreaVector(Mesh, Tris.B);
			const double Dihedral = FMath::RadiansToDegrees(AngleBetween(NormalA, NormalB));
			if (Dihedral > 40.0)
			{
				continue;
			}

			// Orient the edge as it runs in triangle A, so the quad follows the triangles' winding.
			FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
			const FIndex3i TriA = Mesh.GetTriangle(Tris.A);
			if (IndexUtil::FindTriOrderedEdge(Verts.A, Verts.B, TriA) == IndexConstants::InvalidID)
			{
				Swap(Verts.A, Verts.B);
			}
			const int32 C = IndexUtil::FindTriOtherVtx(Verts.A, Verts.B, TriA);
			const int32 D = IndexUtil::FindTriOtherVtx(Verts.A, Verts.B, Mesh.GetTriangle(Tris.B));
			const FIndex4i Quad(Verts.B, C, Verts.A, D);

			const FVector3d Normal = NormalA + NormalB;
			double Score = Dihedral;
			bool bConvex = true;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector3d Prev = Mesh.GetVertex(Quad[(Corner + 3) % 4]);
				const FVector3d Here = Mesh.GetVertex(Quad[Corner]);
				const FVector3d Next = Mesh.GetVertex(Quad[(Corner + 1) % 4]);
				if ((Here - Prev).Cross(Next - Here).Dot(Normal) <= 0.0)
				{
					bConvex = false;
					break;
				}
				Score += FMath::Abs(FMath::RadiansToDegrees(AngleBetween(Prev - Here, Next - Here)) - 90.0);
			}
			if (bConvex && Score < 160.0)
			{
				Candidates.Add({Score, EdgeID, Quad});
			}
		}

		Candidates.Sort([](const FCandidate& L, const FCandidate& R) { return L.Score < R.Score || (L.Score == R.Score && L.Edge < R.Edge); });
		Grid.QuadOfTriangle.Init(INDEX_NONE, Mesh.MaxTriangleID());
		Grid.VertexQuads.SetNum(Mesh.MaxVertexID());
		for (const FCandidate& Candidate : Candidates)
		{
			const FIndex2i Tris = Mesh.GetEdgeT(Candidate.Edge);
			if (Grid.QuadOfTriangle[Tris.A] != INDEX_NONE || Grid.QuadOfTriangle[Tris.B] != INDEX_NONE)
			{
				continue;
			}
			const int32 QuadIndex = Grid.Quads.Add({Candidate.Verts, Tris});
			Grid.QuadOfTriangle[Tris.A] = QuadIndex;
			Grid.QuadOfTriangle[Tris.B] = QuadIndex;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Grid.VertexQuads[Candidate.Verts[Corner]].Add(QuadIndex);
			}
		}
	}

	/** Vertex classes in a once-subdivided quad grid. */
	constexpr int32 ClassOriginal = 0;
	constexpr int32 ClassEdge = 1;
	constexpr int32 ClassFace = 2;

	/** Original and face-center vertices sit opposite each other in a quad; edge midpoints face edge midpoints. */
	int32 OppositeClass(int32 Class)
	{
		return Class == ClassEdge ? ClassEdge : ClassFace - Class;
	}

	/**
	 * Labels vertices with their class in a once-subdivided grid. Around every quad the classes run
	 * original, edge, face, edge, so a quad with one labeled edge gives its other two corners. Labels spread
	 * quad to quad from a seed; a vertex reached with two different classes is marked as a conflict and kept.
	 *
	 * Classes, not grid directions, are tracked on purpose: around a pole (a cube corner, say) the two grid
	 * directions swap, but the classes stay consistent.
	 *
	 * Which corner of the seed quad is original is unknown, so each connected patch is labeled from all four
	 * rotations of the seed and the one with the highest Score is kept.
	 */
	void LabelGrid(const FDynamicMesh3& Mesh, FQuadGrid& Grid, TFunctionRef<int64(const TArray<int32>& Vertices)> Score)
	{
		Grid.Label.Init(INDEX_NONE, Mesh.MaxVertexID());
		Grid.Conflict.Init(false, Mesh.MaxVertexID());
		TArray<bool> Done;
		Done.Init(false, Grid.Quads.Num());
		static constexpr int32 Pattern[4] = {ClassOriginal, ClassEdge, ClassFace, ClassEdge};

		TArray<int32> Vertices;
		TArray<int32> Quads;
		auto SetLabel = [&](int32 Vertex, int32 Label)
		{
			if (Grid.Label[Vertex] == INDEX_NONE)
			{
				Grid.Label[Vertex] = Label;
				Vertices.Add(Vertex);
			}
			else if (Grid.Label[Vertex] != Label)
			{
				Grid.Conflict[Vertex] = true;
			}
		};

		auto Propagate = [&](int32 Seed, int32 Rotation)
		{
			const FIndex4i& SeedVerts = Grid.Quads[Seed].Verts;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				SetLabel(SeedVerts[Corner], Pattern[(Corner + Rotation) % 4]);
			}
			Done[Seed] = true;
			Quads.Add(Seed);

			TArray<int32> Queue;
			auto EnqueueNeighbors = [&](int32 QuadIndex)
			{
				const FIndex4i& Verts = Grid.Quads[QuadIndex].Verts;
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					for (const int32 Other : Grid.VertexQuads[Verts[Corner]])
					{
						if (!Done[Other])
						{
							Queue.Add(Other);
						}
					}
				}
			};
			EnqueueNeighbors(Seed);
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 QuadIndex = Queue[Head];
				if (Done[QuadIndex])
				{
					continue;
				}
				const FIndex4i& Verts = Grid.Quads[QuadIndex].Verts;
				// Needs one fully labeled edge with exactly one edge-midpoint end.
				int32 Edge = INDEX_NONE;
				for (int32 Corner = 0; Corner < 4 && Edge == INDEX_NONE; ++Corner)
				{
					const int32 V0 = Verts[Corner], V1 = Verts[(Corner + 1) % 4];
					const int32 L0 = Grid.Label[V0], L1 = Grid.Label[V1];
					if (L0 != INDEX_NONE && L1 != INDEX_NONE && (L0 == ClassEdge) != (L1 == ClassEdge) &&
						!Grid.Conflict[V0] && !Grid.Conflict[V1])
					{
						Edge = Corner;
					}
				}
				if (Edge == INDEX_NONE)
				{
					continue; // Reached again later from another side, or never.
				}
				Done[QuadIndex] = true;
				Quads.Add(QuadIndex);
				const int32 V0 = Verts[Edge], V1 = Verts[(Edge + 1) % 4], V2 = Verts[(Edge + 2) % 4], V3 = Verts[(Edge + 3) % 4];
				SetLabel(V2, OppositeClass(Grid.Label[V0]));
				SetLabel(V3, OppositeClass(Grid.Label[V1]));
				EnqueueNeighbors(QuadIndex);
			}
		};
		auto Clear = [&]()
		{
			for (const int32 Vertex : Vertices)
			{
				Grid.Label[Vertex] = INDEX_NONE;
				Grid.Conflict[Vertex] = false;
			}
			for (const int32 QuadIndex : Quads)
			{
				Done[QuadIndex] = false;
			}
			Vertices.Reset();
			Quads.Reset();
		};

		for (int32 Seed = 0; Seed < Grid.Quads.Num(); ++Seed)
		{
			if (Done[Seed])
			{
				continue;
			}
			// Seed only quads not yet touched by a labeled patch, so each patch gets its own labeling.
			const FIndex4i& SeedVerts = Grid.Quads[Seed].Verts;
			bool bTouched = false;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				bTouched |= Grid.Label[SeedVerts[Corner]] != INDEX_NONE;
			}
			if (bTouched)
			{
				continue;
			}

			int32 BestRotation = 0;
			int64 BestScore = TNumericLimits<int64>::Lowest();
			for (int32 Rotation = 0; Rotation < 4; ++Rotation)
			{
				Propagate(Seed, Rotation);
				const int64 RotationScore = Score(Vertices);
				if (RotationScore > BestScore)
				{
					BestScore = RotationScore;
					BestRotation = Rotation;
				}
				Clear();
			}
			Propagate(Seed, BestRotation);
			Vertices.Reset();
			Quads.Reset();
		}
	}

	/** One un-subdivide pass. Returns the number of face centers merged. */
	int32 UnsubdivideOnce(FDynamicMesh3& Mesh, TSet<int32>* Selected, PCGUtilsDynMeshDecimate::FResult& Result)
	{
		FRegionDissolver Dissolver(Mesh, Selected);
		FQuadGrid Grid;
		PairQuads(Mesh, Dissolver.RegionOf, Grid);

		auto PolyNeighbors = [&](int32 Vertex, TArray<int32, TInlineAllocator<8>>& Out)
		{
			// Neighbors along quad edges or unpaired-triangle edges, skipping quad diagonals.
			for (const int32 EdgeID : Mesh.VtxEdgesItr(Vertex))
			{
				const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
				const bool bDiagonal = Tris.B != FDynamicMesh3::InvalidID && Grid.QuadOfTriangle[Tris.A] != INDEX_NONE &&
					Grid.QuadOfTriangle[Tris.A] == Grid.QuadOfTriangle[Tris.B];
				if (!bDiagonal)
				{
					const FIndex2i Verts = Mesh.GetEdgeV(EdgeID);
					Out.Add(Verts.A == Vertex ? Verts.B : Verts.A);
				}
			}
		};
		auto IsFaceCenter = [&](int32 Vertex)
		{
			if (Grid.Label[Vertex] != ClassFace || Grid.Conflict[Vertex] ||
				Mesh.IsBoundaryVertex(Vertex) || Dissolver.TouchesLocked(Vertex) ||
				Grid.VertexQuads[Vertex].Num() != 4)
			{
				return false;
			}
			// Every triangle around it must belong to one of its four quads. (That is 6 or 8 triangles,
			// depending on which way the quad diagonals run.)
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(Vertex))
			{
				if (!Grid.VertexQuads[Vertex].Contains(Grid.QuadOfTriangle[TriangleID]))
				{
					return false;
				}
			}
			// The four quads must be nearly coplanar, or merging them would cut across a crease.
			FVector3d CenterNormal = FVector3d::ZeroVector;
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(Vertex))
			{
				CenterNormal += TriangleAreaVector(Mesh, TriangleID);
			}
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(Vertex))
			{
				if (AngleBetween(TriangleAreaVector(Mesh, TriangleID), CenterNormal) > FMath::DegreesToRadians(40.0))
				{
					return false;
				}
			}
			TArray<int32, TInlineAllocator<8>> Neighbors;
			PolyNeighbors(Vertex, Neighbors);
			if (Neighbors.Num() != 4)
			{
				return false;
			}
			for (const int32 Neighbor : Neighbors)
			{
				const int32 Label = Grid.Label[Neighbor];
				if (Label != ClassEdge || Grid.Conflict[Neighbor])
				{
					return false;
				}
			}
			return true;
		};

		// Irregular vertices (poles, corners) must be original vertices in a subdivided mesh, so the labeling
		// that makes them original wins; among equals, the one that finds more face centers.
		TArray<bool> Pole;
		Pole.Init(false, Mesh.MaxVertexID());
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			TArray<int32, TInlineAllocator<8>> Neighbors;
			PolyNeighbors(Vertex, Neighbors);
			Pole[Vertex] = Neighbors.Num() != (Mesh.IsBoundaryVertex(Vertex) ? 3 : 4);
		}
		LabelGrid(Mesh, Grid, [&](const TArray<int32>& Vertices)
		{
			int64 Score = 0;
			for (const int32 Vertex : Vertices)
			{
				if (Grid.Conflict[Vertex])
				{
					continue;
				}
				Score += (Pole[Vertex] && Grid.Label[Vertex] == ClassOriginal) ? 1000 : 0;
				Score += IsFaceCenter(Vertex);
			}
			return Score;
		});

		// Merge the four quads around each face center into one region.
		TArray<bool> Claimed;
		Claimed.Init(false, Grid.Quads.Num());
		int32 Centers = 0;
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			if (!IsFaceCenter(Vertex))
			{
				continue;
			}
			bool bFree = true;
			for (const int32 QuadIndex : Grid.VertexQuads[Vertex])
			{
				bFree &= !Claimed[QuadIndex];
			}
			if (!bFree)
			{
				continue;
			}
			const int32 Region = Grid.Quads[Grid.VertexQuads[Vertex][0]].Tris.A;
			for (const int32 QuadIndex : Grid.VertexQuads[Vertex])
			{
				Claimed[QuadIndex] = true;
				Dissolver.RegionOf[Grid.Quads[QuadIndex].Tris.A] = Region;
				Dissolver.RegionOf[Grid.Quads[QuadIndex].Tris.B] = Region;
			}
			++Centers;
		}
		// Every other quad stays one region of its two triangles.
		for (int32 QuadIndex = 0; QuadIndex < Grid.Quads.Num(); ++QuadIndex)
		{
			if (!Claimed[QuadIndex])
			{
				Dissolver.RegionOf[Grid.Quads[QuadIndex].Tris.B] = Grid.Quads[QuadIndex].Tris.A;
			}
		}
		if (Centers == 0)
		{
			return 0;
		}

		Dissolver.FinalizeRegions();
		Result.Regions = Dissolver.CountRegions();
		Result.BlockedVertices += Dissolver.RemoveInteriorVertices(Result.RemovedVertices);

		TSet<int32> EdgeMidpoints;
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			if (Grid.Label[Vertex] == ClassEdge && !Grid.Conflict[Vertex])
			{
				EdgeMidpoints.Add(Vertex);
			}
		}
		Result.BlockedVertices += Dissolver.RemoveChainVertices(EdgeMidpoints,
			[](int32, int32, int32) { return 0.0; }, Result.RemovedVertices);
		Dissolver.ImproveTriangulation(FMath::DegreesToRadians(10.0));

		// Keep the selection pointing at surviving triangles for the next pass.
		if (Selected)
		{
			for (auto It = Selected->CreateIterator(); It; ++It)
			{
				if (!Mesh.IsTriangle(*It))
				{
					It.RemoveCurrent();
				}
			}
		}
		return Centers;
	}
}

PCGUtilsDynMeshDecimate::FResult PCGUtilsDynMeshDecimate::DissolvePlanar(UE::Geometry::FDynamicMesh3& Mesh,
	const FPlanarOptions& Options, const TSet<int32>* SelectedTriangles)
{
	using namespace PCGUtilsDynMeshDecimateImpl;
	FResult Result;
	FRegionDissolver Dissolver(Mesh, SelectedTriangles);
	FPlanarRegionBuilder(Mesh, Dissolver.RegionOf, Options).Build(Dissolver.RegionOf);
	Dissolver.FinalizeRegions();
	Result.Regions = Dissolver.CountRegions();

	Result.BlockedVertices += Dissolver.RemoveInteriorVertices(Result.RemovedVertices);

	// Outline vertices between two regions (or one region and the open edge of the mesh).
	TSet<int32> Candidates;
	for (const int32 VertexID : Mesh.VertexIndicesItr())
	{
		Candidates.Add(VertexID);
	}
	const double AngleLimit = FMath::DegreesToRadians(FMath::Max(Options.AngleLimitDegrees, 0.0));
	auto Cost = [&](int32 Vertex, int32 A, int32 B) -> double
	{
		if (Options.bAllBoundaries)
		{
			return 0.0;
		}
		const FVector3d Position = Mesh.GetVertex(Vertex);
		// 0 when the two outline edges are in a straight line.
		const double EdgeAngle = UE_DOUBLE_PI - AngleBetween(Mesh.GetVertex(A) - Position, Mesh.GetVertex(B) - Position);
		double VertexCost = EdgeAngle;
		const int32 EdgeID = Mesh.FindEdge(Vertex, A);
		const FIndex2i Tris = Mesh.GetEdgeT(EdgeID);
		if (Tris.B != FDynamicMesh3::InvalidID && !IsDelimited(Mesh, EdgeID, Options))
		{
			// Between two regions: scale by how far apart the two regions bend, so a bent outline between
			// two nearly coplanar regions still goes.
			const double FaceAngle = AngleBetween(Dissolver.RegionNormal[Dissolver.RegionOf[Tris.A]], Dissolver.RegionNormal[Dissolver.RegionOf[Tris.B]]);
			VertexCost = (EdgeAngle / HalfPi) * (FaceAngle / HalfPi) * HalfPi;
		}
		return VertexCost < AngleLimit ? VertexCost : -1.0;
	};
	Result.BlockedVertices += Dissolver.RemoveChainVertices(Candidates, Cost, Result.RemovedVertices);
	Dissolver.ImproveTriangulation(FMath::Max(AngleLimit, FMath::DegreesToRadians(0.5)));
	Mesh.CompactInPlace();
	return Result;
}

PCGUtilsDynMeshDecimate::FResult PCGUtilsDynMeshDecimate::Unsubdivide(UE::Geometry::FDynamicMesh3& Mesh,
	int32 Iterations, const TSet<int32>* SelectedTriangles)
{
	using namespace PCGUtilsDynMeshDecimateImpl;
	FResult Result;
	TOptional<TSet<int32>> Selection;
	if (SelectedTriangles)
	{
		Selection = *SelectedTriangles;
	}
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		const int32 Centers = UnsubdivideOnce(Mesh, Selection.GetPtrOrNull(), Result);
		Result.GridCenters += Centers;
		if (Centers == 0)
		{
			break;
		}
	}
	Mesh.CompactInPlace();
	return Result;
}
