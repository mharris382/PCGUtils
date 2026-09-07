// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

// Template-instantiation typedefs (FDynamicMeshAABBTree3) cannot be safely forward declared under MSVC -
// same reasoning as PCGUtilsDynMeshSurfaceCorrespondence, which this header builds directly on top of.
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

/**
 * Mesh-constrained path solving on a Dynamic Mesh surface: shortest surface paths (geodesics) between projected
 * anchors, and "straight" surface traces from a projected seed and direction.
 *
 * Like PCGUtilsDynMeshSurfaceCorrespondence, this layer knows nothing about PCG - it only needs GeometryCore and
 * the GeometryProcessing DynamicMesh module. The PCG elements (Route Path On DynMesh, Trace Surface Path) supply
 * mesh-local positions and consume mesh-local positions; every coordinate-space decision stays in the elements.
 *
 * ## Why this exists rather than calling UGeometryScriptLibrary_MeshGeodesicFunctions per segment
 *
 * `GetShortestSurfacePath()` deep-copies the entire target mesh on *every* call, because the intrinsic-mesh
 * geodesic can only connect mesh vertices and so has to poke the start/end points in as new vertices. Routing a
 * guide path with N anchors through that wrapper therefore costs N-1 full mesh copies.
 *
 * FSurfaceRouter runs exactly the same engine algorithm (TMeshDijkstra for the initial edge path, then
 * FDeformableEdgePath::Minimize to straighten it) but makes *one* working copy per mesh and pokes *all* anchors
 * into it up front. Poking inserts a vertex on the existing triangle plane, so the surface is unchanged and only
 * the triangulation is refined; every segment then routes vertex-to-vertex with no further mutation.
 *
 * `CreateSurfacePath()` by contrast is already copy-free (it reads through UDynamicMesh::ProcessMesh), so
 * TraceSurfacePath() below simply drives the same UE::Geometry::FMeshGeodesicSurfaceTracer directly against a
 * const mesh - avoiding the UDynamicMesh wrapper PCG data would otherwise have to be pushed through.
 *
 * ## Threading
 *
 * Nothing here touches UObjects, the game thread, or any global state: FSurfaceRouter owns its working mesh, and
 * TraceSurfacePath only reads the mesh it is given. Both are safe on a PCG worker thread. The PCG elements
 * nonetheless declare CanExecuteOnlyOnMainThread, matching every other PCGUtilsDynMesh element, because
 * resolving PCG data and target-actor transforms - not the geometry - is what pins them to the main thread.
 */
namespace PCGUtilsDynMeshSurfacePathing
{
	/** Closest-point correspondence produced by PCGUtilsDynMeshSurfaceCorrespondence::ProjectPoints(). */
	using FSurfaceProjection = PCGUtilsDynMeshSurfaceCorrespondence::FMeshSurfaceProjection;

	/** Outcome of routing one segment between two guide anchors. */
	enum class ESurfaceRouteStatus : uint8
	{
		/** A path was produced. */
		Ok,
		/** The segment's first anchor never projected onto the mesh (or could not be inserted). */
		StartNotOnMesh,
		/** The segment's second anchor never projected onto the mesh (or could not be inserted). */
		EndNotOnMesh,
		/** Both anchors resolved to the same surface location; the segment is empty by construction. */
		Coincident,
		/** No edge path connects the anchors - they lie on separate connected components of the mesh. */
		Disconnected,
	};

	/** Outcome of tracing a straight surface path from one seed. */
	enum class ESurfaceTraceStatus : uint8
	{
		/** A path was produced (see bOutReachedBoundary for whether it ran the full requested length). */
		Ok,
		/** The seed never projected onto the mesh. */
		SeedNotOnMesh,
		/** Max path length was not a positive, finite distance. */
		InvalidPathLength,
		/** The tracer produced fewer than two usable surface points. */
		Degenerate,
	};

	/** A guide anchor after insertion into FSurfaceRouter's working mesh. */
	struct FSurfaceAnchor
	{
		/** Working-mesh vertex the anchor was snapped to or poked in as, or INDEX_NONE when unusable. */
		int32 VertexID = INDEX_NONE;

		/** Mesh-local surface position of the anchor. Meaningful only when bValid. */
		FVector3d Position = FVector3d::Zero();

		bool bValid = false;
	};

	/**
	 * Owns one mutable working copy of a Dynamic Mesh and turns projected guide anchors into real vertices of
	 * that copy, so a whole multi-segment route costs exactly one mesh copy no matter how many segments it has.
	 *
	 * Usage:
	 *   FSurfaceRouter Router(SourceMesh);
	 *   TArray<FSurfaceAnchor> Anchors = Router.InsertAnchors(Projections);   // once per guide path
	 *   Router.RouteSegment(Anchors[i], Anchors[i+1], OutPoints);             // once per segment
	 *
	 * One router serves one set of anchors. InsertAnchors() must be called exactly once, because the projections
	 * it consumes are expressed against the source triangulation and inserting anchors refines that
	 * triangulation - a second batch of source-space projections would name triangles that have since been
	 * split. Route several guide paths across one mesh with one router each; the AABB tree used to produce the
	 * projections is what should be shared between them, not the router.
	 */
	class PCGUTILSDYNMESH_API FSurfaceRouter
	{
	public:
		/** Copies SourceMesh. The copy is the router's own and is refined as anchors are inserted. */
		explicit FSurfaceRouter(const UE::Geometry::FDynamicMesh3& SourceMesh);

		FSurfaceRouter(const FSurfaceRouter&) = delete;
		FSurfaceRouter& operator=(const FSurfaceRouter&) = delete;

		/** True when the working mesh has any triangle to route across. */
		bool IsValid() const { return WorkingMesh.TriangleCount() > 0; }

		/**
		 * Inserts each projection as a vertex of the working mesh. The returned array is index-aligned with
		 * Projections; an entry that did not project (or whose triangle is no longer valid) comes back invalid.
		 *
		 * Projections must have been computed against the *source* mesh this router was constructed from, and
		 * every projection passed in one call is resolved against the same pre-insertion triangulation, so
		 * several anchors landing in one triangle are all placed correctly.
		 *
		 * A projection that already coincides with an existing vertex snaps to it instead of poking a
		 * degenerate sliver triangle.
		 */
		TArray<FSurfaceAnchor> InsertAnchors(TConstArrayView<FSurfaceProjection> Projections);

		/**
		 * Solves the shortest path across the mesh surface from Start to End, appending nothing on failure.
		 * OutPoints is emptied first and, on success, holds mesh-local positions ordered from Start to End,
		 * beginning exactly at Start.Position and ending exactly at End.Position.
		 */
		ESurfaceRouteStatus RouteSegment(
			const FSurfaceAnchor& Start, const FSurfaceAnchor& End, TArray<FVector3d>& OutPoints) const;

		const UE::Geometry::FDynamicMesh3& GetWorkingMesh() const { return WorkingMesh; }

	private:
		UE::Geometry::FDynamicMesh3 WorkingMesh;
	};

	/**
	 * Traces a "straight" (geodesic) path across Mesh from a projected seed, terminating at MaxPathLength or at
	 * a mesh boundary, whichever comes first. Direction is a mesh-local vector projected onto the surface; a
	 * near-zero direction falls back to +Z exactly as UGeometryScriptLibrary_MeshGeodesicFunctions does.
	 *
	 * Mesh is only read - no copy is made and no acceleration structure is required.
	 * OutPoints is emptied first and, on success, holds mesh-local positions ordered from the seed outwards.
	 */
	PCGUTILSDYNMESH_API ESurfaceTraceStatus TraceSurfacePath(
		const UE::Geometry::FDynamicMesh3& Mesh,
		const FSurfaceProjection& Seed,
		const FVector3d& Direction,
		double MaxPathLength,
		TArray<FVector3d>& OutPoints,
		bool& bOutReachedBoundary);

	/**
	 * Appends Segment to InOutPath, dropping Segment's first point when it coincides with the path's current
	 * last point, so consecutive segments meeting at a shared anchor do not produce a duplicated join point.
	 * Tolerance is a distance in mesh-local units.
	 */
	PCGUTILSDYNMESH_API void AppendSegment(
		TArray<FVector3d>& InOutPath, TConstArrayView<FVector3d> Segment, double Tolerance);

	/**
	 * Welding tolerance used for join deduplication, matching the coalesce threshold Epic's geodesic solver
	 * already applies to adjacent path points.
	 */
	inline constexpr double PathJoinTolerance = 0.01;
}
