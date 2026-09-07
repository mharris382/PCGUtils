// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Templates/UniquePtr.h"

namespace UE::Geometry { struct FDynamicSubmesh3; }

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
	};

	/**
	 * Builds PCGUtils path data from an ordered run of mesh-local positions: one UPCGPointArrayData whose points
	 * are ordered along the path, carrying the closed-loop @Data flag, matching what DynMesh Selection To Paths
	 * emits. A closed path must already have had its repeated final point removed by the caller.
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
