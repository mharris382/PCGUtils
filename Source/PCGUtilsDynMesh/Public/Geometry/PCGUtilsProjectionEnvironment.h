// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Engine/EngineTypes.h"

class AActor;
class UWorld;

/**
 * What a projection traces against.
 *
 * One virtual call on a path that already costs a scene query, bought for two reasons. It keeps world collision
 * from being wired into the projection algorithm, so a Dynamic Mesh, a Landscape proxy or an explicit target can
 * be substituted later without touching the solver. And it is what makes the whole feature testable: a plane
 * environment turns "does this settle correctly on a slope" into an exact arithmetic assertion, with no map, no
 * physics scene and no editor.
 *
 * Implementations are read-only and must tolerate being called repeatedly from one thread.
 */
class PCGUTILSDYNMESH_API IPCGUtilsProjectionEnvironment
{
public:
	virtual ~IPCGUtilsProjectionEnvironment() = default;

	/**
	 * Traces a single ray.
	 *
	 * @param OutLocation  The impact point. Untouched on a miss.
	 * @param OutNormal    The surface normal at the impact, pointing out of the surface. Untouched on a miss.
	 * @return false when nothing was hit between InStart and InEnd.
	 */
	virtual bool Trace(
		const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const = 0;

	/** Human-readable name for diagnostics, e.g. "world collision" or "target mesh". */
	virtual FString Describe() const = 0;
};

/**
 * An infinite analytic plane.
 *
 * Primarily a test double - it makes "rests exactly on the surface" checkable to machine precision - but it is
 * also the cheapest possible ground for a graph that just needs a flat datum.
 */
class PCGUTILSDYNMESH_API FPCGUtilsPlaneProjectionEnvironment final : public IPCGUtilsProjectionEnvironment
{
public:
	/** @param InPlaneNormal Need not be unit length; it is normalised on construction. */
	FPCGUtilsPlaneProjectionEnvironment(const FVector& InPointOnPlane, const FVector& InPlaneNormal);

	virtual bool Trace(
		const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const override;
	virtual FString Describe() const override { return TEXT("plane"); }

private:
	FVector PointOnPlane;
	FVector PlaneNormal;
};

/**
 * A Dynamic Mesh, traced through its own AABB tree.
 *
 * No world, no physics, no collision setup - so this is the environment a graph uses to settle fragments against
 * geometry that only exists inside the graph, and the one the automated tests use for anything shaped.
 *
 * The mesh must outlive this object; the tree holds a pointer to it.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProjectionEnvironment final : public IPCGUtilsProjectionEnvironment
{
public:
	/** @param InMeshToWorld Mesh-local -> the space rays are given in. */
	FPCGUtilsDynMeshProjectionEnvironment(
		const UE::Geometry::FDynamicMesh3& InMesh, const FTransform& InMeshToWorld);

	virtual bool Trace(
		const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const override;
	virtual FString Describe() const override { return TEXT("target mesh"); }

	/** False when the mesh had no triangles to build a tree from; every trace then misses. */
	bool IsUsable() const { return Tree.IsValid() && Tree->GetMesh() != nullptr; }

private:
	const UE::Geometry::FDynamicMesh3& Mesh;
	FTransform MeshToWorld;
	FTransform WorldToMesh;
	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> Tree;
};

/** Query parameters for the world environment, named to match PCG's own world-query vocabulary. */
struct PCGUTILSDYNMESH_API FPCGUtilsWorldProjectionQueryParams
{
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldStatic;

	/** Trace against render geometry rather than simplified collision. Accurate and markedly slower. */
	bool bTraceComplex = false;

	/** Actors excluded from the trace, typically the PCG target actor so a fragment cannot land on itself. */
	TArray<const AActor*> IgnoredActors;
};

/**
 * Level collision, through `UWorld::LineTraceSingleByChannel`.
 *
 * Scene queries are read-only against the physics acceleration structure, and PCG's own world-query elements run
 * them from worker threads, so this does not force the calling element onto the game thread. It does need a
 * `UWorld`, which an automation context has no reason to provide - hence the mesh and plane environments above.
 */
class PCGUTILSDYNMESH_API FPCGUtilsWorldProjectionEnvironment final : public IPCGUtilsProjectionEnvironment
{
public:
	FPCGUtilsWorldProjectionEnvironment(const UWorld* InWorld, FPCGUtilsWorldProjectionQueryParams InParams);

	virtual bool Trace(
		const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const override;
	virtual FString Describe() const override { return TEXT("world collision"); }

	bool IsUsable() const { return World != nullptr; }

private:
	const UWorld* World = nullptr;
	FPCGUtilsWorldProjectionQueryParams Params;
};
