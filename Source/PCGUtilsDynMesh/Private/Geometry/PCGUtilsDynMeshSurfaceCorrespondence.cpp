// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "MeshQueries.h"
#include "Spatial/SpatialInterfaces.h"

namespace PCGUtilsDynMeshSurfaceCorrespondence
{
	using namespace UE::Geometry;

	namespace
	{
		bool IsFinite(const FVector3d& V)
		{
			return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
		}

		/** Closest-point query for one already-transformed (source-space) point. */
		FMeshSurfaceProjection ProjectOne(
			const FDynamicMeshAABBTree3& SourceTree, const FDynamicMesh3& SourceMesh,
			const FVector3d& SourcePoint, double MaxDistance)
		{
			FMeshSurfaceProjection Out;
			if (!IsFinite(SourcePoint))
			{
				return Out;
			}

			IMeshSpatial::FQueryOptions QueryOptions;
			QueryOptions.MaxDistance = MaxDistance;

			double NearestDistSqr = TNumericLimits<double>::Max();
			const int32 TriangleID = SourceTree.FindNearestTriangle(SourcePoint, NearestDistSqr, QueryOptions);
			if (TriangleID < 0 || !SourceMesh.IsTriangle(TriangleID))
			{
				return Out;
			}

			// TriangleDistance() computes the closest-point solve (and its barycentric coordinates) internally
			// before returning; NearestDistSqr from the tree query is the same squared distance.
			FDistPoint3Triangle3d Query =
				TMeshQueries<FDynamicMesh3>::TriangleDistance(SourceMesh, TriangleID, SourcePoint);

			const FVector3d Bary = Query.TriangleBaryCoords;
			if (!IsFinite(Bary))
			{
				return Out;
			}

			Out.SourceTriangleID = TriangleID;
			Out.BarycentricCoordinates = Bary;
			Out.DistanceSquared = NearestDistSqr;
			Out.bProjected = true;
			return Out;
		}

		FMeshSurfaceProjectionResult ProjectTransformedPoints(
			const FDynamicMeshAABBTree3& SourceTree,
			TFunctionRef<FVector3d(int32)> GetSourceSpacePoint,
			TFunctionRef<bool(int32)> IsValidIndex,
			int32 Count,
			const FProjectionOptions& Options)
		{
			FMeshSurfaceProjectionResult Result;
			Result.Projections.SetNum(Count);

			const FDynamicMesh3* SourceMesh = SourceTree.GetMesh();
			if (!SourceMesh || SourceMesh->TriangleCount() == 0)
			{
				Result.bSourceUnavailable = true;
				Result.NumFailed = Count;
				return Result;
			}

			const double MaxDistance = Options.MaxDistance;
			auto Body = [&](int32 Index)
			{
				if (!IsValidIndex(Index))
				{
					return;
				}
				Result.Projections[Index] = ProjectOne(SourceTree, *SourceMesh, GetSourceSpacePoint(Index), MaxDistance);
			};

			if (Options.bParallel && Count > 256)
			{
				ParallelFor(Count, Body);
			}
			else
			{
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Body(Index);
				}
			}

			for (const FMeshSurfaceProjection& Projection : Result.Projections)
			{
				(Projection.bProjected ? Result.NumProjected : Result.NumFailed)++;
			}
			return Result;
		}
	}

	FMeshSurfaceProjectionResult ProjectPoints(
		const FDynamicMeshAABBTree3& SourceTree,
		TConstArrayView<FVector3d> DestinationPoints,
		const FProjectionOptions& Options)
	{
		const FTransform& DestinationToSource = Options.DestinationToSource;
		const bool bIdentity = DestinationToSource.Equals(FTransform::Identity);

		return ProjectTransformedPoints(
			SourceTree,
			[&DestinationPoints, &DestinationToSource, bIdentity](int32 Index)
			{
				const FVector3d& P = DestinationPoints[Index];
				return bIdentity ? P : FVector3d(DestinationToSource.TransformPosition(P));
			},
			[](int32) { return true; },
			DestinationPoints.Num(),
			Options);
	}

	FMeshSurfaceProjectionResult ProjectMeshVertices(
		const FDynamicMeshAABBTree3& SourceTree,
		const FDynamicMesh3& DestinationMesh,
		const FProjectionOptions& Options)
	{
		const FTransform& DestinationToSource = Options.DestinationToSource;
		const bool bIdentity = DestinationToSource.Equals(FTransform::Identity);

		return ProjectTransformedPoints(
			SourceTree,
			[&DestinationMesh, &DestinationToSource, bIdentity](int32 VertexID)
			{
				const FVector3d P = DestinationMesh.GetVertex(VertexID);
				return bIdentity ? P : FVector3d(DestinationToSource.TransformPosition(P));
			},
			[&DestinationMesh](int32 VertexID) { return DestinationMesh.IsVertex(VertexID); },
			DestinationMesh.MaxVertexID(),
			Options);
	}

	bool SampleColorOverlay(
		const FMeshSurfaceProjection& Projection,
		const FDynamicMeshColorOverlay& SourceColors,
		FVector4f& OutColor)
	{
		if (!Projection.bProjected || !SourceColors.IsSetTriangle(Projection.SourceTriangleID))
		{
			return false;
		}

		FVector4f A, B, C;
		SourceColors.GetTriElements(Projection.SourceTriangleID, A, B, C);

		const FVector3d& Bary = Projection.BarycentricCoordinates;
		OutColor = static_cast<float>(Bary.X) * A
			+ static_cast<float>(Bary.Y) * B
			+ static_cast<float>(Bary.Z) * C;
		return true;
	}

	int32 TransferColorChannels(
		const FMeshSurfaceProjectionResult& Projection,
		const FDynamicMeshColorOverlay& SourceColors,
		EColorChannelBits Channels,
		TArrayView<FVector4f> InOutDestColors)
	{
		const uint8 Mask = static_cast<uint8>(Channels);
		if (Mask == 0)
		{
			return 0;
		}

		const int32 Count = FMath::Min(Projection.Projections.Num(), InOutDestColors.Num());
		int32 NumModified = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FVector4f Sampled;
			if (!SampleColorOverlay(Projection.Projections[Index], SourceColors, Sampled))
			{
				continue;
			}

			FVector4f& Dest = InOutDestColors[Index];
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				if ((Mask & (1 << Channel)) != 0)
				{
					Dest[Channel] = Sampled[Channel];
				}
			}
			++NumModified;
		}
		return NumModified;
	}
}
