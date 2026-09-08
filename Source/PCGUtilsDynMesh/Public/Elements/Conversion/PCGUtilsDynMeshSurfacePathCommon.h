// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Templates/UniquePtr.h"

namespace UE::Geometry { struct FDynamicSubmesh3; }

class UPCGBasePointData;
class UPCGData;
class UPCGDynamicMeshData;
class UPCGPointArrayData;
class UPCGUtilsDynMeshProcessBaseSettings;
struct FPCGContext;

/**
 * The PCG-facing half of the DynMesh surface-pathing feature, shared by Route Path On DynMesh and
 * Trace Surface Path: resolving a process input into a queryable surface, and turning an ordered run of
 * mesh-local positions back into PCGUtils path data.
 *
 * The geometry itself lives in Geometry/PCGUtilsDynMeshSurfacePathing.h, which has no PCG dependency at all.
 */
namespace PCGUtilsDynMeshSurfacePathCommon
{
	/**
	 * One process input resolved down to the surface paths are solved against, plus the acceleration structure
	 * every projection and normal lookup for that input reuses.
	 *
	 * A Selection input restricts pathing to the selected triangle region, extracted as a submesh exactly as
	 * Sample DynMesh does. Positions in a submesh are the source mesh's own positions, so nothing needs to be
	 * mapped back; only the triangle IDs differ, and no surface-pathing output exposes those.
	 *
	 * Move-only: the AABB tree points at the mesh this struct owns (or at the source PCG data's mesh), so the
	 * two must travel together.
	 */
	struct PCGUTILSDYNMESH_API FResolvedSurface
	{
		FResolvedSurface() = default;
		FResolvedSurface(FResolvedSurface&&) = default;
		FResolvedSurface& operator=(FResolvedSurface&&) = default;
		FResolvedSurface(const FResolvedSurface&) = delete;
		FResolvedSurface& operator=(const FResolvedSurface&) = delete;
		~FResolvedSurface();

		/** Source DynMesh data, used to resolve the target actor transform. Never null when IsValid(). */
		const UPCGDynamicMeshData* MeshData = nullptr;

		/** The surface to path across: the whole source mesh, or the selection's submesh. */
		const UE::Geometry::FDynamicMesh3* Mesh = nullptr;

		/** Owns Mesh when a Selection input restricted the surface; null for a whole-mesh input. */
		TUniquePtr<UE::Geometry::FDynamicSubmesh3> Submesh;

		/** Built once per input and reused for every projection and every output-point normal lookup. */
		TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> Tree;

		bool IsValid() const { return MeshData && Mesh && Tree.IsValid() && Mesh->TriangleCount() > 0; }
	};

	/**
	 * Resolves one input through the standard process resolver (materialized Selection, optional Selector,
	 * domain conversion), extracts the selected region when there is one, and builds the AABB tree.
	 *
	 * Returns an invalid surface after logging an actionable graph warning naming NodeTitle. An empty selection
	 * is a valid "no work" answer and also comes back invalid, without a warning.
	 */
	PCGUTILSDYNMESH_API FResolvedSurface ResolveSurface(
		const UPCGData* InputData,
		const UPCGUtilsDynMeshProcessBaseSettings* Settings,
		FPCGContext* Context,
		const FText& NodeTitle);

	/**
	 * Where one output path point's values come from in the guide path that produced it.
	 *
	 * A routed path has more points than the guide path it came from - the geodesic solver inserts a point at
	 * every triangle crossing - so an output point almost never lines up with a guide point. Each output point
	 * instead records the two guide points that bracket it and how far along that span it sits, which is enough
	 * to both pick the nearest guide point and blend between the pair.
	 *
	 * StartIndex == EndIndex means the point sits exactly on one guide point; Alpha is then ignored.
	 */
	struct FPathSourceRef
	{
		/** Index of the guide point at Alpha = 0. */
		int32 StartIndex = INDEX_NONE;

		/** Index of the guide point at Alpha = 1. */
		int32 EndIndex = INDEX_NONE;

		/** Normalized arc-length position between StartIndex and EndIndex, in [0, 1]. */
		float Alpha = 0.0f;

		bool IsValid() const { return StartIndex != INDEX_NONE && EndIndex != INDEX_NONE; }

		/** The guide point this output point is closest to along the span. */
		int32 NearestIndex() const { return (Alpha < 0.5f) ? StartIndex : EndIndex; }
	};

	/**
	 * How the output path inherits from the input path it is a mutation of.
	 *
	 * A surface-routed path is the guide path moved onto the mesh, not a new dataset, so the output is
	 * initialized from SourceData: it keeps the guide path's attributes in every metadata domain (@Data
	 * included), its target actor, and its per-point values. PointSources then says, for each output point,
	 * which guide points to take those values from.
	 *
	 * Leaving SourceData null builds a standalone path with no source to inherit from - the right answer for a
	 * node whose output does not correspond to any input point data.
	 */
	struct FPathSourceInheritance
	{
		/** The input path the output is a mutation of. Null disables inheritance entirely. */
		const UPCGBasePointData* SourceData = nullptr;

		/** Index-aligned with the positions passed to BuildPathData. Must match them in count when SourceData is set. */
		TConstArrayView<FPathSourceRef> PointSources;

		/**
		 * Blend attributes that allow interpolation (and Density/Color/Bounds) between the two bracketing guide
		 * points, instead of copying everything from the nearest one. Attributes that do not allow
		 * interpolation - strings, names, soft object paths, bools - always come from the nearest guide point.
		 */
		bool bInterpolate = true;
	};

	/** Everything the shared path builder needs that is not the geometry itself. */
	struct FPathOutputOptions
	{
		/** Mesh-local -> output space. Identity keeps the path in the DynMesh's own coordinate space. */
		FTransform MeshToOutput = FTransform::Identity;

		/** Written to the closed-loop @Data attribute; also drives tangent wrap-around at the path ends. */
		bool bClosed = false;

		/** Name of the closed-loop @Data Bool attribute. Nothing is written when this is None. */
		FName IsClosedAttributeName = NAME_None;

		/** Steepness assigned to every generated point. */
		float PointSteepness = 1.0f;

		/**
		 * What input path this output is a mutation of, and where each output point's values come from in it.
		 * Default-constructed (SourceData null) means no inheritance, and points are built from scratch.
		 */
		FPathSourceInheritance Inheritance;
	};

	/**
	 * Builds PCGUtils path data from an ordered run of mesh-local positions: one UPCGPointArrayData whose points
	 * are ordered along the path, carrying the closed-loop @Data flag, matching what DynMesh Selection To Paths
	 * emits. A closed path must already have had its repeated final point removed by the caller.
	 *
	 * When Options.Inheritance names a source path, the result is that path mutated rather than a new dataset:
	 * it is initialized from the source (attributes in every metadata domain, target actor) and every output
	 * point takes its metadata entry, Density, Color and Bounds from the guide points its FPathSourceRef names.
	 * Point count is deliberately not inherited - a routed path has its own - so spatial-data inheritance is off.
	 *
	 * Transform, Steepness and Seed are always the node's own: the frame is the whole point of routing, Steepness
	 * comes from Options.PointSteepness, and Seed is recomputed from the routed position so that points which
	 * share a guide point do not also share a seed.
	 *
	 * The one deliberate addition over DynMesh Selection To Paths is point orientation. That node emits boundary
	 * loops with identity rotations, but a surface-following path feeds spline conversion, vine/cable mesh
	 * spawning and path-based scatter, all of which need a frame. Frames follow the PCGUtilsDynMesh sampling
	 * convention (see Sample DynMesh): X is the path tangent, Z is the mesh surface normal.
	 *
	 * NormalSource supplies each point's surface normal and must be a built tree over the same mesh the positions
	 * came from. Passing the tree rather than the mesh is deliberate - both callers already build one per input
	 * for projection, so normals cost one closest-triangle query each instead of a second structure.
	 *
	 * Returns null for a null context or fewer than two positions.
	 */
	PCGUTILSDYNMESH_API UPCGPointArrayData* BuildPathData(
		FPCGContext* Context,
		TConstArrayView<FVector3d> MeshLocalPositions,
		const UE::Geometry::FDynamicMeshAABBTree3& NormalSource,
		const FPathOutputOptions& Options);
}
