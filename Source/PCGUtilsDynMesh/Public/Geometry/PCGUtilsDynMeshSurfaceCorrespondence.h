// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/EnumClassFlags.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
	template<class TriangleMeshType> class TMeshAABBTree3;
	using FDynamicMeshAABBTree3 = TMeshAABBTree3<FDynamicMesh3>;
	template<typename RealType, int ElementSize, typename VectorType>
	class TDynamicMeshVectorOverlay;
	using FDynamicMeshColorOverlay = TDynamicMeshVectorOverlay<float, 4, FVector4f>;
}

/**
 * General, reusable surface-correspondence utility: for each destination sample, find the closest point on a
 * source Dynamic Mesh surface and record the source triangle plus barycentric coordinates, so any per-corner
 * source attribute (vertex color, UV, weight, ...) can afterwards be resampled onto the destination by
 * barycentric interpolation.
 *
 * This module knows nothing about the Painter framework, Static Mesh Components, LOD policy, or PCG element
 * execution. It only needs GeometryCore. It is intended for transferring mesh attributes between two
 * representations of the same surface (for example LOD0 -> a lower LOD of one Static Mesh), and is written so
 * a caller can cache the returned correspondence and reuse it for several attributes without recomputing the
 * projection.
 *
 * ## Coordinate-space contract
 *
 * The source `FDynamicMeshAABBTree3` defines the reference space ("source space"). Every destination point is
 * transformed by `DestinationToSource` (destination space -> source space) before it is queried; the default
 * is identity, i.e. destination and source already share a space. Two LODs of the same Static Mesh asset
 * ordinarily share the asset's local space, so identity is correct there. `SourceTriangleID` and
 * `BarycentricCoordinates` in the result are expressed against the source mesh's topology; `DistanceSquared`
 * is measured in source space (after the transform).
 */
namespace PCGUtilsDynMeshSurfaceCorrespondence
{
	/** One destination sample's closest-point correspondence onto the source surface. */
	struct FMeshSurfaceProjection
	{
		/** Source triangle the closest point lies on, or INDEX_NONE when projection failed. */
		int32 SourceTriangleID = INDEX_NONE;

		/** Barycentric weights of the closest point in `SourceTriangleID`, corner order (V0,V1,V2). Sums to 1. */
		FVector3d BarycentricCoordinates = FVector3d::ZeroVector;

		/** Squared distance from the (transformed) destination point to the closest source point, in source space. */
		double DistanceSquared = TNumericLimits<double>::Max();

		/** True when `SourceTriangleID` is a valid triangle and the barycentric coordinates are finite. */
		bool bProjected = false;
	};

	/** Result of projecting a set of destination samples. `Projections` is index-aligned with the input. */
	struct FMeshSurfaceProjectionResult
	{
		TArray<FMeshSurfaceProjection> Projections;

		/** Entries with `bProjected == true`. */
		int32 NumProjected = 0;

		/** Entries that could not be projected (empty/degenerate source, outside `MaxDistance`, non-finite input). */
		int32 NumFailed = 0;

		/** True when the source tree/mesh had no queryable triangle; every entry then failed. */
		bool bSourceUnavailable = false;

		bool AllProjected() const { return NumFailed == 0 && !bSourceUnavailable; }
	};

	/** Options shared by the projection entry points. */
	struct FProjectionOptions
	{
		/** Destination-space -> source-space transform applied to every destination point. Identity by default. */
		FTransform DestinationToSource = FTransform::Identity;

		/** Reject a correspondence whose closest source point is farther than this (source-space units). */
		double MaxDistance = TNumericLimits<double>::Max();

		/** Run the closest-point queries with ParallelFor. Safe: the source tree is only read. */
		bool bParallel = true;
	};

	/**
	 * Projects an explicit array of destination points onto `SourceTree`'s mesh. `SourceTree` must already be
	 * built and must outlive the call. Output is index-aligned with `DestinationPoints`.
	 */
	PCGUTILSDYNMESH_API FMeshSurfaceProjectionResult ProjectPoints(
		const UE::Geometry::FDynamicMeshAABBTree3& SourceTree,
		TConstArrayView<UE::Geometry::FVector3d> DestinationPoints,
		const FProjectionOptions& Options = FProjectionOptions());

	/**
	 * Projects the vertices of `DestinationMesh` onto `SourceTree`'s mesh. The result is indexed by destination
	 * vertex ID (size `DestinationMesh.MaxVertexID()`); ID gaps from a sparse Dynamic Mesh are left with
	 * `bProjected == false` and never read the destination geometry out of bounds.
	 */
	PCGUTILSDYNMESH_API FMeshSurfaceProjectionResult ProjectMeshVertices(
		const UE::Geometry::FDynamicMeshAABBTree3& SourceTree,
		const UE::Geometry::FDynamicMesh3& DestinationMesh,
		const FProjectionOptions& Options = FProjectionOptions());

	/**
	 * Barycentrically samples the source primary color overlay at one projection. Returns false (and leaves
	 * `OutColor` untouched) when the projection failed or the source triangle has no overlay data.
	 * Interpolation is done in linear float space; no FColor round-trip.
	 */
	PCGUTILSDYNMESH_API bool SampleColorOverlay(
		const FMeshSurfaceProjection& Projection,
		const UE::Geometry::FDynamicMeshColorOverlay& SourceColors,
		FVector4f& OutColor);

	/** Channel bit-flags for TransferColorChannels. Values match the natural RGBA order but are independent. */
	enum class EColorChannelBits : uint8
	{
		None = 0,
		R = 1 << 0,
		G = 1 << 1,
		B = 1 << 2,
		A = 1 << 3,
		All = 0x0F
	};
	ENUM_CLASS_FLAGS(EColorChannelBits)

	/**
	 * For every destination entry that projected, samples `SourceColors` and copies only the `Channels` bits of
	 * the interpolated linear color into `InOutDestColors[i]`, leaving the other channels (and every
	 * non-projected entry) exactly as the caller supplied them. `InOutDestColors` must be index-aligned with the
	 * projection result. Nothing is quantized here — the caller owns the final FColor / sRGB write boundary.
	 *
	 * @return number of destination entries actually modified.
	 */
	PCGUTILSDYNMESH_API int32 TransferColorChannels(
		const FMeshSurfaceProjectionResult& Projection,
		const UE::Geometry::FDynamicMeshColorOverlay& SourceColors,
		EColorChannelBits Channels,
		TArrayView<FVector4f> InOutDestColors);
}
