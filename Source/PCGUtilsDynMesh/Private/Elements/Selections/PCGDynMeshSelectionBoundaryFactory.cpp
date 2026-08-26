// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshSelectionBoundaryFactory.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSelectionBoundaryFactory"

namespace
{
	class FSelectionBoundaryOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectionBoundaryOperation(const UPCGDynMeshSelectionBoundaryFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !Factory->RegionFactory || !InSelectionContext.MeshData)
			{
				return false;
			}

			FPCGUtilsDynMeshSelectionDomain SourceDomain;
			SourceDomain.ElementType = UE::Geometry::EGeometryElementType::Face;
			SourceDomain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
			if (!Factory->RegionFactory->SupportsDomain(SourceDomain))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("UnsupportedSourceDomain", "Select Boundary could not adapt its child selector to the required triangle region domain."),
					Context);
				return false;
			}

			SourceEvaluationContext = MakeUnique<FPCGUtilsDynMeshSelectionEvaluationContext>(
				InSelectionContext.MeshData, InSelectionContext.Mesh, SourceDomain);
			TSharedPtr<FPCGUtilsDynMeshSelectionOperation> RegionOperation =
				Factory->RegionFactory->CreateOperation(Context);
			if (!RegionOperation || !RegionOperation->Initialize(*SourceEvaluationContext))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ChildInitializationFailed", "Select Boundary could not initialize its child region operation."),
					Context);
				return false;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			UE::Geometry::FGeometrySelection RegionSelection;
			RegionSelection.InitializeTypes(SourceDomain.ElementType, SourceDomain.TopologyType);
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				if (RegionOperation->TestElement(TriangleID))
				{
					RegionSelection.Selection.Add(
						UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
				}
			}
			if (RegionSelection.IsEmpty())
			{
				return true;
			}

			FGeometryScriptMeshSelection ScriptRegion;
			ScriptRegion.SetSelection(MoveTemp(RegionSelection));
			FGeometryScriptMeshSelection ScriptBoundary;
			const UDynamicMesh* DynamicMesh = InSelectionContext.MeshData->GetDynamicMesh();
			if (!DynamicMesh)
			{
				return false;
			}
			UGeometryScriptLibrary_MeshSelectionFunctions::SelectSelectionBoundaryEdges(
				const_cast<UDynamicMesh*>(DynamicMesh), ScriptRegion, ScriptBoundary,
				Factory->bExcludeMeshBoundaryEdges);

			TArray<int32> EdgeIDs;
			if (ScriptBoundary.ConvertToMeshIndexArray(
				Mesh, EdgeIDs, EGeometryScriptIndexType::Edge) != EGeometryScriptIndexType::Edge)
			{
				return false;
			}
			BoundaryEdgeIDs.Reserve(EdgeIDs.Num());
			for (const int32 EdgeID : EdgeIDs)
			{
				if (Mesh.IsEdge(EdgeID))
				{
					BoundaryEdgeIDs.Add(EdgeID);
				}
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return BoundaryEdgeIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGDynMeshSelectionBoundaryFactoryData> Factory;
		TUniquePtr<FPCGUtilsDynMeshSelectionEvaluationContext> SourceEvaluationContext;
		TSet<int32> BoundaryEdgeIDs;
	};
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshSelectionBoundaryFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSelectionBoundaryOperation>(this);
}

void UPCGDynMeshSelectionBoundaryFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		bool bExclude = bExcludeMeshBoundaryEdges;
		uint32 ChildCrc = RegionFactory ? RegionFactory->GetOrComputeCrc(true).GetValue() : 0;
		Ar << bExclude;
		Ar << ChildCrc;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DEPRECATED: Select Boundary Provider");
}

TArray<FText> UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("BoundaryEdgesAlias", "Boundary Edges Selector"),
		LOCTEXT("OutlineAlias", "Selection Outline Selector")
	};
}

FText UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Deprecated compatibility node. Use Select Boundary with Operation Mode set to Selector.");
}
#endif

UPCGUtilsDynMeshFactoryData* UPCGSelectionBoundaryEdgesSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> RegionFactories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, PCGDynMeshSelectionBoundaryFactoryConstants::RegionFactoryInputPin,
		RegionFactories, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return nullptr;
	}

	if (RegionFactories.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneFactory", "Select Boundary requires exactly one region selector in Selector mode."),
			InContext);
		return nullptr;
	}

	UPCGDynMeshSelectionBoundaryFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshSelectionBoundaryFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshSelectionBoundaryFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->RegionFactory = RegionFactories[0];
	Factory->bExcludeMeshBoundaryEdges = bExcludeMeshBoundaryEdges;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
