#include "Elements/Selections/PCGSelectSelfOcclusion.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "Spatial/SpatialInterfaces.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectSelfOcclusion"

#if WITH_EDITOR
FText UPCGSelectSelfOcclusionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select Self Occlusion");
}

TArray<FText> UPCGSelectSelfOcclusionSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("SelfRaycastAlias", "Select by Self Raycast"),
		LOCTEXT("MeshOcclusionAlias", "Select by Mesh Occlusion")
	};
}

FText UPCGSelectSelfOcclusionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Selects Dynamic Mesh vertices or triangles whose ray is blocked (or unblocked) by the same mesh. An incoming selection is converted to the configured element domain and is used to prune the traced candidates.");
}
#endif

FPCGElementPtr UPCGSelectSelfOcclusionSettings::CreateElement() const
{
	return MakeShared<FPCGSelectSelfOcclusionElement>();
}

bool FPCGSelectSelfOcclusionElement::ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context, UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSelectSelfOcclusionSettings* Settings = Context->GetInputSettings<UPCGSelectSelfOcclusionSettings>();
	check(Settings && MeshData);

	FVector TraceDirection = Settings->TraceDirection;
	if (Settings->DirectionSpace == EPCGDynMeshSelfOcclusionDirectionSpace::World)
	{
		const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			Context, MeshData, /*bConvertToLocalSpace=*/true);
		TraceDirection = ActorTransform.InverseTransformVectorNoScale(TraceDirection);
	}
	if (!TraceDirection.Normalize())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ZeroTraceDirection", "Select Self Occlusion requires a non-zero Trace Direction."), Context);
		return false;
	}

	const FVector3d Direction(TraceDirection);
	const double NormalOffset = FMath::Max(0.0, Settings->NormalOffset);
	const double DirectionOffset = FMath::Max(0.0, Settings->DirectionOffset);
	const double MaximumDistance = Settings->MaximumDistance > 0.0
		? Settings->MaximumDistance : TNumericLimits<double>::Max();
	const bool bWantOccluded = Settings->Result == EPCGDynMeshSelfOcclusionResult::Occluded;

	FDynamicMeshAABBTree3 Spatial(&Mesh, /*bAutoBuild=*/true);

	auto RayMatches = [&](const FVector3d& Position, const FVector3d& Normal,
		int32 SourceVertexID, int32 SourceTriangleID)
	{
		FVector3d SafeNormal = Normal;
		if (!SafeNormal.Normalize())
		{
			SafeNormal = FVector3d::UnitZ();
		}
		const FVector3d Origin = Position + SafeNormal * NormalOffset + Direction * DirectionOffset;

		IMeshSpatial::FQueryOptions QueryOptions;
		QueryOptions.MaxDistance = MaximumDistance;
		if (Settings->bIgnoreSourceTriangles)
		{
			QueryOptions.TriangleFilterF = [&Mesh, SourceVertexID, SourceTriangleID](int32 TriangleID)
			{
				if (TriangleID == SourceTriangleID)
				{
					return false;
				}
				return SourceVertexID == INDEX_NONE || !Mesh.GetTriangle(TriangleID).Contains(SourceVertexID);
			};
		}

		double HitDistance = 0.0;
		int32 HitTriangleID = INDEX_NONE;
		FVector3d HitBarycentrics = FVector3d::Zero();
		const bool bOccluded = Spatial.FindNearestHitTriangle(
			FRay3d(Origin, Direction), HitDistance, HitTriangleID, HitBarycentrics, QueryOptions);
		return bOccluded == bWantOccluded;
	};

	if (Settings->ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex)
	{
		OutSelection.InitializeTypes(EGeometryElementType::Vertex, EGeometryTopologyType::Triangle);

		FMeshNormals VertexNormals(&Mesh);
		if (Mesh.HasAttributes() && Mesh.Attributes()->PrimaryNormals())
		{
			VertexNormals.GetVertexNormalsFromOverlayNormals(FMeshNormals::ECombineSplitNormalsMethod::Average);
		}
		else
		{
			VertexNormals.ComputeVertexNormals();
		}
		const TArray<FVector3d>& Normals = VertexNormals.GetNormals();

		Candidates.ProcessVertices([&](int32 VertexID)
		{
			if (!Mesh.IsVertex(VertexID))
			{
				return;
			}
			const FVector3d Normal = Normals.IsValidIndex(VertexID) ? Normals[VertexID] : FVector3d::UnitZ();
			if (RayMatches(Mesh.GetVertex(VertexID), Normal, VertexID, INDEX_NONE))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
		});
	}
	else
	{
		OutSelection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
		Candidates.ProcessTriangles([&](int32 TriangleID)
		{
			if (!Mesh.IsTriangle(TriangleID))
			{
				return;
			}
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			if (RayMatches((A + B + C) / 3.0, Mesh.GetTriNormal(TriangleID), INDEX_NONE, TriangleID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		});
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
