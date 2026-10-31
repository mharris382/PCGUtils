// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/**
 * Ray occlusion against a set of DynMeshes, shared by Select | By Mesh Occlusion and the PCGEx Mesh Occlusion
 * point filter.
 *
 * Each occluder is a mesh plus the transform that places it in one shared space (normally world space, from the
 * mesh's PCG target actor). Rays are given in that shared space, and so are distances. A ray is carried into each
 * mesh's own space for the query, so occluders with non-uniform scale need no baked copy.
 */
namespace PCGUtilsDynMeshOcclusion
{
	/**
	 * The element a ray starts from, when it lies on one of the occluders. Its own triangle (or every triangle
	 * around its vertex) is skipped for that occluder, so a ray never hits the surface it leaves.
	 */
	struct FSourceElement
	{
		int32 Occluder = INDEX_NONE;
		int32 VertexID = INDEX_NONE;
		int32 TriangleID = INDEX_NONE;
	};

	class PCGUTILSDYNMESH_API FOccluderSet
	{
	public:
		FOccluderSet();
		~FOccluderSet();
		FOccluderSet(const FOccluderSet&) = delete;
		FOccluderSet& operator=(const FOccluderSet&) = delete;

		/**
		 * Adds Mesh, placed in the shared space by ToShared, and builds its spatial index. The mesh must stay alive
		 * and unchanged while the set is used. Returns the occluder's index, or INDEX_NONE (and adds nothing) when
		 * the mesh has no triangles.
		 */
		int32 Add(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& ToShared);

		int32 Num() const { return Occluders.Num(); }
		bool IsEmpty() const { return Occluders.IsEmpty(); }

		/**
		 * Finds the nearest occluder hit along a ray, in shared-space units. Direction must be unit length;
		 * MaxDistance <= 0 means unlimited. Safe to call from many threads at once.
		 */
		bool FindNearestHit(const FVector3d& Origin, const FVector3d& Direction, double MaxDistance,
			double& OutDistance, const FSourceElement* Ignore = nullptr) const;

		bool IsOccluded(const FVector3d& Origin, const FVector3d& Direction, double MaxDistance,
			const FSourceElement* Ignore = nullptr) const
		{
			double Distance = 0.0;
			return FindNearestHit(Origin, Direction, MaxDistance, Distance, Ignore);
		}

	private:
		struct FOccluder;
		TArray<TUniquePtr<FOccluder>> Occluders;
	};

	/**
	 * Carries a surface normal through a transform, using the inverse transpose so it stays perpendicular to the
	 * surface under non-uniform scale. Returns a unit vector, or the rotated input if a scale axis is zero.
	 */
	PCGUTILSDYNMESH_API FVector3d TransformNormal(const FTransform& Transform, const FVector3d& Normal);
}
