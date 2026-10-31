// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshOcclusion.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/SpatialInterfaces.h"

namespace PCGUtilsDynMeshOcclusion
{
	struct FOccluderSet::FOccluder
	{
		const UE::Geometry::FDynamicMesh3* Mesh = nullptr;
		TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> Tree;
		FTransform ToShared = FTransform::Identity;
	};

	FOccluderSet::FOccluderSet() = default;
	FOccluderSet::~FOccluderSet() = default;

	int32 FOccluderSet::Add(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& ToShared)
	{
		if (Mesh.TriangleCount() == 0)
		{
			return INDEX_NONE;
		}
		TUniquePtr<FOccluder> Occluder = MakeUnique<FOccluder>();
		Occluder->Mesh = &Mesh;
		Occluder->Tree = MakeUnique<UE::Geometry::FDynamicMeshAABBTree3>(&Mesh, /*bAutoBuild=*/true);
		Occluder->ToShared = ToShared;
		return Occluders.Add(MoveTemp(Occluder));
	}

	bool FOccluderSet::FindNearestHit(const FVector3d& Origin, const FVector3d& Direction, double MaxDistance,
		double& OutDistance, const FSourceElement* Ignore) const
	{
		using namespace UE::Geometry;
		const bool bLimited = MaxDistance > 0.0 && FMath::IsFinite(MaxDistance);
		double Best = bLimited ? MaxDistance : TNumericLimits<double>::Max();
		bool bHit = false;

		for (int32 Index = 0; Index < Occluders.Num(); ++Index)
		{
			const FOccluder& Occluder = *Occluders[Index];
			// The ray O + t*D maps to o + t*d in the mesh's space, so t (the shared-space distance) is unchanged
			// when the local query runs along d normalized and its distances are divided by |d|.
			const FVector3d LocalOrigin = Occluder.ToShared.InverseTransformPosition(Origin);
			const FVector3d LocalVector = Occluder.ToShared.InverseTransformVector(Direction);
			const double Scale = LocalVector.Length();
			if (Scale <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			IMeshSpatial::FQueryOptions Options;
			Options.MaxDistance = Best < TNumericLimits<double>::Max() / Scale ? Best * Scale : TNumericLimits<double>::Max();
			if (Ignore && Ignore->Occluder == Index)
			{
				const FDynamicMesh3* Mesh = Occluder.Mesh;
				const int32 SourceVertex = Ignore->VertexID;
				const int32 SourceTriangle = Ignore->TriangleID;
				Options.TriangleFilterF = [Mesh, SourceVertex, SourceTriangle](int32 TriangleID)
				{
					if (TriangleID == SourceTriangle)
					{
						return false;
					}
					return SourceVertex == INDEX_NONE || !Mesh->GetTriangle(TriangleID).Contains(SourceVertex);
				};
			}

			double LocalDistance = 0.0;
			int32 TriangleID = INDEX_NONE;
			FVector3d Barycentrics = FVector3d::Zero();
			if (Occluder.Tree->FindNearestHitTriangle(FRay3d(LocalOrigin, LocalVector / Scale), LocalDistance,
				TriangleID, Barycentrics, Options))
			{
				const double Distance = LocalDistance / Scale;
				if (Distance <= Best)
				{
					Best = Distance;
					bHit = true;
				}
			}
		}
		if (bHit)
		{
			OutDistance = Best;
		}
		return bHit;
	}

	FVector3d TransformNormal(const FTransform& Transform, const FVector3d& Normal)
	{
		const FVector3d Scale = Transform.GetScale3D();
		FVector3d Scaled = Normal;
		if (!FMath::IsNearlyZero(Scale.X) && !FMath::IsNearlyZero(Scale.Y) && !FMath::IsNearlyZero(Scale.Z))
		{
			Scaled = FVector3d(Normal.X / Scale.X, Normal.Y / Scale.Y, Normal.Z / Scale.Z);
		}
		FVector3d Result = Transform.GetRotation().RotateVector(Scaled);
		return Result.Normalize() ? Result : Transform.GetRotation().RotateVector(Normal);
	}
}
