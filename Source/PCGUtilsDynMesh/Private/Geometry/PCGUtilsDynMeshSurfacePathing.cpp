// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshSurfacePathing.h"

#include "Algo/Reverse.h"
#include "DynamicMesh/InfoTypes.h"
#include "MeshQueries.h"
#include "Operations/GeodesicPath.h"
#include "Operations/IntrinsicCorrespondenceUtils.h"
#include "Operations/MeshGeodesicSurfaceTracer.h"
#include "Parameterization/MeshDijkstra.h"

namespace PCGUtilsDynMeshSurfacePathing
{
	using namespace UE::Geometry;

	namespace
	{
		/** A barycentric weight this close to 1 means the point is already sitting on that triangle corner. */
		constexpr double VertexSnapTolerance = 1.0e-4;

		bool IsFinite(const FVector3d& V)
		{
			return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
		}

		/**
		 * Clamps tiny negative barycentric weights produced by the closest-point solve to zero and renormalizes,
		 * so PokeTriangle and the surface tracer both see a strictly valid coordinate. Mirrors the sanitization
		 * UGeometryScriptLibrary_MeshGeodesicFunctions applies to Blueprint-supplied coordinates.
		 */
		FVector3d SanitizeBarycentric(const FVector3d& Bary)
		{
			FVector3d Result = Bary;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (!FMath::IsFinite(Result[Index]) || Result[Index] < 0.0)
				{
					Result[Index] = FMath::Max(0.0, FMath::IsFinite(Result[Index]) ? Result[Index] : 0.0);
				}
			}

			const double Sum = Result.X + Result.Y + Result.Z;
			if (!(Sum > 0.0) || !FMath::IsFinite(Sum))
			{
				return FVector3d(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);
			}
			return Result * (1.0 / Sum);
		}

		/**
		 * The engine's surface tracer can stall on a vertex crossing and then keep reporting the same surface
		 * point. Give up after this many consecutive non-advancing results rather than emitting a path made of
		 * thousands of coincident points.
		 */
		constexpr int32 MaxStalledTraceSteps = 8;

		/**
		 * Drops points that repeat their predecessor within Tolerance, always retaining the exact first and last
		 * point. Both solvers coalesce internally, but pinning a segment's endpoints back onto its anchors can
		 * still leave a coincident pair at the very end, which would then survive into the output path as a
		 * duplicated join. Leaves fewer than two points when the whole run collapses to one location.
		 */
		void CompactConsecutivePoints(TArray<FVector3d>& Points, double Tolerance)
		{
			if (Points.Num() < 2)
			{
				return;
			}

			const FVector3d LastPoint = Points.Last();
			const double ToleranceSqr = Tolerance * Tolerance;

			TArray<FVector3d> Compacted;
			Compacted.Reserve(Points.Num());
			Compacted.Add(Points[0]);
			for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
			{
				if (FVector3d::DistSquared(Compacted.Last(), Points[Index]) > ToleranceSqr)
				{
					Compacted.Add(Points[Index]);
				}
			}

			if (FVector3d::DistSquared(Compacted.Last(), LastPoint) > ToleranceSqr)
			{
				Compacted.Add(LastPoint);
			}
			else
			{
				// The run ends where it already was: keep the exact endpoint, not the point it collapsed onto.
				Compacted.Last() = LastPoint;
			}

			Points = MoveTemp(Compacted);
		}

		/** Index of the largest barycentric weight. */
		int32 DominantCorner(const FVector3d& Bary)
		{
			int32 Best = 0;
			for (int32 Index = 1; Index < 3; ++Index)
			{
				if (Bary[Index] > Bary[Best])
				{
					Best = Index;
				}
			}
			return Best;
		}
	}

	FSurfaceRouter::FSurfaceRouter(const FDynamicMesh3& SourceMesh)
		: WorkingMesh(SourceMesh)
	{
	}

	TArray<FSurfaceAnchor> FSurfaceRouter::InsertAnchors(TConstArrayView<FSurfaceProjection> Projections)
	{
		TArray<FSurfaceAnchor> Anchors;
		Anchors.SetNum(Projections.Num());

		if (!IsValid())
		{
			return Anchors;
		}

		// Working triangle/barycentric state per anchor. Kept separate from the returned anchors because it is
		// rewritten as earlier pokes retriangulate the triangles later anchors were resolved against.
		struct FPending
		{
			int32 TriangleID = INDEX_NONE;
			FVector3d Bary = FVector3d::Zero();
			bool bLive = false;
		};

		TArray<FPending> Pending;
		Pending.SetNum(Projections.Num());

		// Resolve every anchor's surface position first, against the *pre-insertion* triangulation. Positions
		// stay meaningful no matter how the triangulation is later refined, so they are what the fix-up below
		// re-resolves against.
		for (int32 Index = 0; Index < Projections.Num(); ++Index)
		{
			const FSurfaceProjection& Projection = Projections[Index];
			if (!Projection.bProjected || !WorkingMesh.IsTriangle(Projection.SourceTriangleID))
			{
				continue;
			}

			const FVector3d Bary = SanitizeBarycentric(Projection.BarycentricCoordinates);
			const FVector3d Position = WorkingMesh.GetTriBaryPoint(Projection.SourceTriangleID, Bary.X, Bary.Y, Bary.Z);
			if (!IsFinite(Position))
			{
				continue;
			}

			Pending[Index] = FPending{Projection.SourceTriangleID, Bary, true};
			Anchors[Index].Position = Position;
		}

		for (int32 Index = 0; Index < Pending.Num(); ++Index)
		{
			FPending& Anchor = Pending[Index];
			if (!Anchor.bLive || !WorkingMesh.IsTriangle(Anchor.TriangleID))
			{
				continue;
			}

			// Already on a triangle corner: reuse that vertex rather than poking a degenerate sliver.
			const int32 Corner = DominantCorner(Anchor.Bary);
			if (Anchor.Bary[Corner] >= 1.0 - VertexSnapTolerance)
			{
				const int32 VertexID = WorkingMesh.GetTriangle(Anchor.TriangleID)[Corner];
				if (WorkingMesh.IsVertex(VertexID))
				{
					Anchors[Index].VertexID = VertexID;
					Anchors[Index].Position = WorkingMesh.GetVertex(VertexID);
					Anchors[Index].bValid = true;
					continue;
				}
			}

			DynamicMeshInfo::FPokeTriangleInfo PokeInfo;
			const int32 PokedTriangleID = Anchor.TriangleID;
			if (WorkingMesh.PokeTriangle(PokedTriangleID, Anchor.Bary, PokeInfo) != EMeshResult::Ok
				|| !WorkingMesh.IsVertex(PokeInfo.NewVertex))
			{
				continue;
			}

			Anchors[Index].VertexID = PokeInfo.NewVertex;
			Anchors[Index].Position = WorkingMesh.GetVertex(PokeInfo.NewVertex);
			Anchors[Index].bValid = true;

			// PokeTriangle re-uses PokedTriangleID for one of the three sub-triangles, so any anchor still
			// waiting on that triangle now holds a stale (triangle, barycentric) pair even though its ID may
			// still be a valid triangle. Re-resolve those against the three sub-triangles by closest point.
			const FIndex3i Candidates(PokedTriangleID, PokeInfo.NewTriangles.A, PokeInfo.NewTriangles.B);
			for (int32 Other = Index + 1; Other < Pending.Num(); ++Other)
			{
				FPending& Later = Pending[Other];
				if (!Later.bLive || Later.TriangleID != PokedTriangleID)
				{
					continue;
				}

				const FVector3d& Target = Anchors[Other].Position;
				double BestDistanceSqr = TNumericLimits<double>::Max();
				int32 BestTriangleID = INDEX_NONE;
				FVector3d BestBary = FVector3d::Zero();
				for (int32 Slot = 0; Slot < 3; ++Slot)
				{
					const int32 CandidateID = Candidates[Slot];
					if (!WorkingMesh.IsTriangle(CandidateID))
					{
						continue;
					}
					const FDistPoint3Triangle3d Query =
						TMeshQueries<FDynamicMesh3>::TriangleDistance(WorkingMesh, CandidateID, Target);
					// TriangleDistance() has already run the closest-point solve; GetSquared() is non-const and
					// would recompute it, so measure against the closest point it already stored.
					const double DistanceSqr = FVector3d::DistSquared(Target, Query.ClosestTrianglePoint);
					if (DistanceSqr < BestDistanceSqr)
					{
						BestDistanceSqr = DistanceSqr;
						BestTriangleID = CandidateID;
						BestBary = Query.TriangleBaryCoords;
					}
				}

				if (BestTriangleID == INDEX_NONE || !IsFinite(BestBary))
				{
					Later.bLive = false;
					continue;
				}
				Later.TriangleID = BestTriangleID;
				Later.Bary = SanitizeBarycentric(BestBary);
			}
		}

		return Anchors;
	}

	ESurfaceRouteStatus FSurfaceRouter::RouteSegment(
		const FSurfaceAnchor& Start, const FSurfaceAnchor& End, TArray<FVector3d>& OutPoints) const
	{
		OutPoints.Reset();

		if (!Start.bValid || !WorkingMesh.IsVertex(Start.VertexID))
		{
			return ESurfaceRouteStatus::StartNotOnMesh;
		}
		if (!End.bValid || !WorkingMesh.IsVertex(End.VertexID))
		{
			return ESurfaceRouteStatus::EndNotOnMesh;
		}
		if (Start.VertexID == End.VertexID)
		{
			return ESurfaceRouteStatus::Coincident;
		}

		// Seed at the end vertex and walk back from the start vertex: FindPathToNearestSeed returns the path
		// beginning at the queried point, so this already comes out ordered start -> end.
		TArray<int32> VertexPath;
		{
			using FMeshDijkstra = TMeshDijkstra<FDynamicMesh3>;
			FMeshDijkstra Dijkstra(&WorkingMesh);

			TArray<FMeshDijkstra::FSeedPoint> SeedPoints;
			SeedPoints.AddDefaulted_GetRef().PointID = End.VertexID;

			Dijkstra.ComputeToTargetPoint(SeedPoints, Start.VertexID);
			Dijkstra.FindPathToNearestSeed(Start.VertexID, VertexPath);
		}

		if (VertexPath.Num() < 2)
		{
			return ESurfaceRouteStatus::Disconnected;
		}

		TArray<FEdgePath::FDirectedSegment> DirectedSegments;
		DirectedSegments.Reserve(VertexPath.Num() - 1);
		for (int32 Index = 1; Index < VertexPath.Num(); ++Index)
		{
			const int32 HeadVID = VertexPath[Index];
			const int32 EdgeID = WorkingMesh.FindEdge(VertexPath[Index - 1], HeadVID);
			if (EdgeID == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			FEdgePath::FDirectedSegment& Segment = DirectedSegments.AddDefaulted_GetRef();
			Segment.EID = EdgeID;
			Segment.HeadIndex = (WorkingMesh.GetEdgeV(EdgeID).B == HeadVID) ? 1 : 0;
		}

		if (DirectedSegments.IsEmpty())
		{
			return ESurfaceRouteStatus::Disconnected;
		}

		// Straighten the Dijkstra edge path into the true surface geodesic using the engine's intrinsic-mesh
		// solver - the same code path GetShortestSurfacePath() drives, but against our shared working mesh.
		FDeformableEdgePath DeformableEdgePath(WorkingMesh, DirectedSegments);
		FDeformableEdgePath::FEdgePathDeformationInfo DeformationInfo;
		DeformableEdgePath.Minimize(DeformationInfo);

		const TArray<FDeformableEdgePath::FSurfacePoint> SurfacePoints =
			DeformableEdgePath.AsSurfacePoints(PathJoinTolerance);
		const FDynamicMesh3* SurfaceMesh = DeformableEdgePath.GetIntrinsicMesh().GetNormalCoordinates().SurfaceMesh;
		if (!SurfaceMesh)
		{
			return ESurfaceRouteStatus::Disconnected;
		}

		OutPoints.Reserve(SurfacePoints.Num());
		for (const FDeformableEdgePath::FSurfacePoint& SurfacePoint : SurfacePoints)
		{
			bool bValidPoint = false;
			const FVector3d Position = IntrinsicCorrespondenceUtils::AsR3Position(SurfacePoint, *SurfaceMesh, bValidPoint);
			if (bValidPoint && IsFinite(Position))
			{
				OutPoints.Add(Position);
			}
		}

		if (OutPoints.Num() < 2)
		{
			OutPoints.Reset();
			return ESurfaceRouteStatus::Disconnected;
		}

		// The solver's tail/head convention is an implementation detail; anchor the result to the caller's
		// requested direction explicitly rather than relying on it.
		if (FVector3d::DistSquared(OutPoints[0], Start.Position)
			> FVector3d::DistSquared(OutPoints.Last(), Start.Position))
		{
			Algo::Reverse(OutPoints);
		}

		// Pin the endpoints exactly, so consecutive segments join on identical coordinates.
		OutPoints[0] = Start.Position;
		OutPoints.Last() = End.Position;

		CompactConsecutivePoints(OutPoints, PathJoinTolerance);
		if (OutPoints.Num() < 2)
		{
			// The whole segment collapsed onto one location: the anchors are distinct vertices but sit closer
			// together than the path tolerance, so there is no meaningful geometry to contribute.
			OutPoints.Reset();
			return ESurfaceRouteStatus::Coincident;
		}

		return ESurfaceRouteStatus::Ok;
	}

	ESurfaceTraceStatus TraceSurfacePath(
		const FDynamicMesh3& Mesh,
		const FSurfaceProjection& Seed,
		const FVector3d& Direction,
		double MaxPathLength,
		TArray<FVector3d>& OutPoints,
		bool& bOutReachedBoundary)
	{
		OutPoints.Reset();
		bOutReachedBoundary = false;

		if (!Seed.bProjected || !Mesh.IsTriangle(Seed.SourceTriangleID))
		{
			return ESurfaceTraceStatus::SeedNotOnMesh;
		}
		if (!(MaxPathLength > 0.0) || !FMath::IsFinite(MaxPathLength))
		{
			return ESurfaceTraceStatus::InvalidPathLength;
		}

		// Matches CreateSurfacePath(): a degenerate direction falls back to +Z rather than failing.
		const FVector3d TraceDirection = Direction.GetSafeNormal(1.0e-5, FVector3d::UnitZ());
		const FVector3d StartBary = SanitizeBarycentric(Seed.BarycentricCoordinates);

		FMeshGeodesicSurfaceTracer Tracer(Mesh);
		Tracer.TraceMeshFromBaryPoint(Seed.SourceTriangleID, StartBary, TraceDirection, MaxPathLength);

		const TArray<FMeshGeodesicSurfaceTracer::FTraceResult>& Trace = Tracer.GetTraceResults();
		OutPoints.Reserve(Trace.Num());
		int32 StalledSteps = 0;
		for (const FMeshGeodesicSurfaceTracer::FTraceResult& TracePoint : Trace)
		{
			if (TracePoint.TriID == IndexConstants::InvalidID || !Mesh.IsTriangle(TracePoint.TriID))
			{
				break;
			}

			const FVector3d Position = Mesh.GetTriBaryPoint(
				TracePoint.TriID, TracePoint.Barycentric[0], TracePoint.Barycentric[1], TracePoint.Barycentric[2]);
			if (!IsFinite(Position))
			{
				break;
			}

			// A trace that arrives at a mesh vertex can start reporting the same surface point indefinitely.
			// Stop once it has clearly stopped advancing; the path so far is still valid.
			if (!OutPoints.IsEmpty()
				&& FVector3d::DistSquared(OutPoints.Last(), Position) <= PathJoinTolerance * PathJoinTolerance)
			{
				if (++StalledSteps >= MaxStalledTraceSteps)
				{
					break;
				}
				continue;
			}

			StalledSteps = 0;
			OutPoints.Add(Position);

			if (TracePoint.Classification == FMeshGeodesicSurfaceTracer::ETraceClassification::BoundaryTerminated)
			{
				bOutReachedBoundary = true;
			}
		}

		CompactConsecutivePoints(OutPoints, PathJoinTolerance);

		if (OutPoints.Num() < 2)
		{
			OutPoints.Reset();
			bOutReachedBoundary = false;
			return ESurfaceTraceStatus::Degenerate;
		}

		return ESurfaceTraceStatus::Ok;
	}

	void AppendSegment(TArray<FVector3d>& InOutPath, TConstArrayView<FVector3d> Segment, double Tolerance)
	{
		if (Segment.IsEmpty())
		{
			return;
		}

		int32 First = 0;
		if (!InOutPath.IsEmpty()
			&& FVector3d::DistSquared(InOutPath.Last(), Segment[0]) <= Tolerance * Tolerance)
		{
			First = 1;
		}

		InOutPath.Reserve(InOutPath.Num() + (Segment.Num() - First));
		for (int32 Index = First; Index < Segment.Num(); ++Index)
		{
			InOutPath.Add(Segment[Index]);
		}
	}
}
