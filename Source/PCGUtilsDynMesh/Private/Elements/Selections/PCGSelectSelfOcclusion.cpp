#include "Elements/Selections/PCGSelectSelfOcclusion.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Spatial/SpatialInterfaces.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectSelfOcclusion"

namespace
{
	class FSelectSelfOcclusionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectSelfOcclusionOperation(const UPCGSelectSelfOcclusionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !InSelectionContext.MeshData)
			{
				return false;
			}

			FVector ResolvedDirection = Factory->TraceDirection;
			if (Factory->DirectionSpace == EPCGDynMeshSelfOcclusionDirectionSpace::World)
			{
				const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
					Context, InSelectionContext.MeshData, /*bConvertToLocalSpace=*/true);
				ResolvedDirection = ActorTransform.InverseTransformVectorNoScale(ResolvedDirection);
			}
			if (!ResolvedDirection.Normalize())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ZeroTraceDirection", "Select by Self Occlusion requires a non-zero Trace Direction."), Context);
				return false;
			}

			Direction = FVector3d(ResolvedDirection);
			NormalOffset = FMath::Max(0.0, Factory->NormalOffset);
			DirectionOffset = FMath::Max(0.0, Factory->DirectionOffset);
			MaximumDistance = Factory->MaximumDistance > 0.0
				? Factory->MaximumDistance : TNumericLimits<double>::Max();
			bWantOccluded = Factory->Result == EPCGDynMeshSelfOcclusionResult::Occluded;
			Spatial = MakeUnique<UE::Geometry::FDynamicMeshAABBTree3>(
				&InSelectionContext.Mesh, /*bAutoBuild=*/true);

			if (Factory->ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex)
			{
				VertexNormals = MakeUnique<UE::Geometry::FMeshNormals>(&InSelectionContext.Mesh);
				if (InSelectionContext.Mesh.HasAttributes() &&
					InSelectionContext.Mesh.Attributes()->PrimaryNormals())
				{
					VertexNormals->GetVertexNormalsFromOverlayNormals(
						UE::Geometry::FMeshNormals::ECombineSplitNormalsMethod::Average);
				}
				else
				{
					VertexNormals->ComputeVertexNormals();
				}
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			using namespace UE::Geometry;
			const FDynamicMesh3& Mesh = SelectionContext->Mesh;
			FVector3d Position;
			FVector3d Normal;
			int32 SourceVertexID = INDEX_NONE;
			int32 SourceTriangleID = INDEX_NONE;
			if (Factory->ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex)
			{
				if (!Mesh.IsVertex(ElementID)) return false;
				Position = Mesh.GetVertex(ElementID);
				const TArray<FVector3d>& Normals = VertexNormals->GetNormals();
				Normal = Normals.IsValidIndex(ElementID) ? Normals[ElementID] : FVector3d::UnitZ();
				SourceVertexID = ElementID;
			}
			else
			{
				if (!Mesh.IsTriangle(ElementID)) return false;
				FVector3d A, B, C;
				Mesh.GetTriVertices(ElementID, A, B, C);
				Position = (A + B + C) / 3.0;
				Normal = Mesh.GetTriNormal(ElementID);
				SourceTriangleID = ElementID;
			}

			if (!Normal.Normalize()) Normal = FVector3d::UnitZ();
			const FVector3d Origin = Position + Normal * NormalOffset + Direction * DirectionOffset;
			IMeshSpatial::FQueryOptions QueryOptions;
			QueryOptions.MaxDistance = MaximumDistance;
			if (Factory->bIgnoreSourceTriangles)
			{
				QueryOptions.TriangleFilterF = [&Mesh, SourceVertexID, SourceTriangleID](int32 TriangleID)
				{
					if (TriangleID == SourceTriangleID) return false;
					return SourceVertexID == INDEX_NONE || !Mesh.GetTriangle(TriangleID).Contains(SourceVertexID);
				};
			}

			double HitDistance = 0.0;
			int32 HitTriangleID = INDEX_NONE;
			FVector3d HitBarycentrics = FVector3d::Zero();
			const bool bOccluded = Spatial->FindNearestHitTriangle(
				FRay3d(Origin, Direction), HitDistance, HitTriangleID, HitBarycentrics, QueryOptions);
			return bOccluded == bWantOccluded;
		}

	private:
		TObjectPtr<const UPCGSelectSelfOcclusionFactoryData> Factory;
		TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> Spatial;
		TUniquePtr<UE::Geometry::FMeshNormals> VertexNormals;
		FVector3d Direction = FVector3d::UnitZ();
		double MaximumDistance = TNumericLimits<double>::Max();
		double NormalOffset = 0.0;
		double DirectionOffset = 0.0;
		bool bWantOccluded = true;
	};
}

#if WITH_EDITOR
FText UPCGSelectSelfOcclusionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select by Self Occlusion");
}

FText UPCGSelectSelfOcclusionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Selects Dynamic Mesh vertices or triangles whose ray is blocked (or unblocked) by the same mesh. An incoming selection is converted to the configured element domain and is used to prune the traced candidates.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGSelectSelfOcclusionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSelectSelfOcclusionOperation>(this);
}

void UPCGSelectSelfOcclusionFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ElementTypeValue = static_cast<uint8>(ElementType);
		uint8 ResultValue = static_cast<uint8>(Result);
		uint8 DirectionSpaceValue = static_cast<uint8>(DirectionSpace);
		FVector DirectionValue = TraceDirection;
		double MaximumDistanceValue = MaximumDistance;
		double NormalOffsetValue = NormalOffset;
		double DirectionOffsetValue = DirectionOffset;
		bool bIgnore = bIgnoreSourceTriangles;
		Ar << ElementTypeValue << ResultValue << DirectionValue << DirectionSpaceValue;
		Ar << MaximumDistanceValue << NormalOffsetValue << DirectionOffsetValue << bIgnore;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGSelectSelfOcclusionSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGSelectSelfOcclusionFactoryData* Factory = InFactory
		? Cast<UPCGSelectSelfOcclusionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSelectSelfOcclusionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}
	Factory->Priority = Priority;
	Factory->ElementType = ElementType;
	Factory->Result = Result;
	Factory->TraceDirection = TraceDirection;
	Factory->DirectionSpace = DirectionSpace;
	Factory->MaximumDistance = MaximumDistance;
	Factory->NormalOffset = NormalOffset;
	Factory->DirectionOffset = DirectionOffset;
	Factory->bIgnoreSourceTriangles = bIgnoreSourceTriangles;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
