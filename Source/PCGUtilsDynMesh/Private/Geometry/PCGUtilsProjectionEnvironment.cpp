// Copyright Max Harris

#include "Geometry/PCGUtilsProjectionEnvironment.h"

#include "CollisionQueryParams.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// --- Plane ---------------------------------------------------------------------------------------------

FPCGUtilsPlaneProjectionEnvironment::FPCGUtilsPlaneProjectionEnvironment(
	const FVector& InPointOnPlane, const FVector& InPlaneNormal)
	: PointOnPlane(InPointOnPlane)
	, PlaneNormal(InPlaneNormal.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector))
{
}

bool FPCGUtilsPlaneProjectionEnvironment::Trace(
	const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const
{
	const FVector Segment = InEnd - InStart;
	const double Denominator = FVector::DotProduct(Segment, PlaneNormal);
	if (FMath::IsNearlyZero(Denominator))
	{
		// Parallel to the plane: either no intersection, or the whole segment lies in it. Neither is a contact
		// a caller can do anything useful with.
		return false;
	}

	const double Alpha = FVector::DotProduct(PointOnPlane - InStart, PlaneNormal) / Denominator;
	if (Alpha < 0.0 || Alpha > 1.0)
	{
		return false;
	}

	OutLocation = InStart + Segment * Alpha;
	// Always report the side the ray came from, so a fragment starting below the plane is pushed back up
	// against a normal that still points at it rather than away.
	OutNormal = Denominator < 0.0 ? PlaneNormal : -PlaneNormal;
	return true;
}

// --- Dynamic Mesh --------------------------------------------------------------------------------------

FPCGUtilsDynMeshProjectionEnvironment::FPCGUtilsDynMeshProjectionEnvironment(
	const UE::Geometry::FDynamicMesh3& InMesh, const FTransform& InMeshToWorld)
	: Mesh(InMesh)
	, MeshToWorld(InMeshToWorld)
	, WorldToMesh(InMeshToWorld.Inverse())
{
	if (InMesh.TriangleCount() > 0)
	{
		Tree = MakeUnique<UE::Geometry::FDynamicMeshAABBTree3>(&InMesh, /*bAutoBuild=*/true);
	}
}

bool FPCGUtilsDynMeshProjectionEnvironment::Trace(
	const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const
{
	if (!IsUsable())
	{
		return false;
	}

	// The tree is in mesh space, so the ray goes to it rather than the mesh coming to the ray - which also
	// keeps the tree valid across however many traces one projection runs.
	const FVector LocalStart = WorldToMesh.TransformPosition(InStart);
	const FVector LocalEnd = WorldToMesh.TransformPosition(InEnd);
	const FVector LocalSegment = LocalEnd - LocalStart;
	const double LocalLength = LocalSegment.Length();
	if (LocalLength <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const FRay3d Ray(LocalStart, LocalSegment / LocalLength);

	UE::Geometry::IMeshSpatial::FQueryOptions Options;
	// Bounding the query is what keeps Maximum Distance meaningful, and it lets the tree reject most of the
	// mesh before descending.
	Options.MaxDistance = LocalLength;

	double HitDistance = 0.0;
	int32 HitTriangleID = INDEX_NONE;
	FVector3d Barycentrics = FVector3d::Zero();
	if (!Tree->FindNearestHitTriangle(Ray, HitDistance, HitTriangleID, Barycentrics, Options)
		|| HitTriangleID == INDEX_NONE)
	{
		return false;
	}

	OutLocation = MeshToWorld.TransformPosition(Ray.PointAt(HitDistance));

	// A triangle normal rather than an interpolated vertex normal: this is a contact query, and the flat facet
	// is what the fragment would actually rest on. TransformVectorNoScale keeps it a direction under a scaled
	// target; a non-uniformly scaled one would need the inverse-transpose, which is not worth it here.
	const FVector3d LocalNormal = Mesh.GetTriNormal(HitTriangleID);
	OutNormal = MeshToWorld.TransformVectorNoScale(LocalNormal).GetSafeNormal(
		UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);

	// Report the face the ray arrived at, so a sample starting inside the target still gets a normal pointing
	// back at it. Matches the plane environment.
	if (FVector::DotProduct(OutNormal, InEnd - InStart) > 0.0)
	{
		OutNormal = -OutNormal;
	}
	return true;
}

// --- World collision -----------------------------------------------------------------------------------

FPCGUtilsWorldProjectionEnvironment::FPCGUtilsWorldProjectionEnvironment(
	const UWorld* InWorld, FPCGUtilsWorldProjectionQueryParams InParams)
	: World(InWorld)
	, Params(MoveTemp(InParams))
{
}

bool FPCGUtilsWorldProjectionEnvironment::Trace(
	const FVector& InStart, const FVector& InEnd, FVector& OutLocation, FVector& OutNormal) const
{
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PCGUtilsProjection), Params.bTraceComplex);
	QueryParams.bReturnPhysicalMaterial = false;
	for (const AActor* Actor : Params.IgnoredActors)
	{
		if (Actor)
		{
			QueryParams.AddIgnoredActor(Actor);
		}
	}

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, InStart, InEnd, Params.CollisionChannel, QueryParams))
	{
		return false;
	}

	OutLocation = Hit.ImpactPoint;
	OutNormal = Hit.ImpactNormal;
	return true;
}
