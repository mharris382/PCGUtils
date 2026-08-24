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
	FPCGUtilsDynMeshSelectionDomain MakeSourceDomain(
		EPCGUtilsDynMeshBoundarySourceElementType SourceElementType)
	{
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
		switch (SourceElementType)
		{
		case EPCGUtilsDynMeshBoundarySourceElementType::Vertex:
			Domain.ElementType = UE::Geometry::EGeometryElementType::Vertex;
			break;
		case EPCGUtilsDynMeshBoundarySourceElementType::Edge:
			Domain.ElementType = UE::Geometry::EGeometryElementType::Edge;
			break;
		case EPCGUtilsDynMeshBoundarySourceElementType::Triangle:
		default:
			Domain.ElementType = UE::Geometry::EGeometryElementType::Face;
			break;
		}
		return Domain;
	}

	const TCHAR* GetSourceDomainName(EPCGUtilsDynMeshBoundarySourceElementType SourceElementType)
	{
		switch (SourceElementType)
		{
		case EPCGUtilsDynMeshBoundarySourceElementType::Vertex: return TEXT("Vertex Region");
		case EPCGUtilsDynMeshBoundarySourceElementType::Edge: return TEXT("Edge Region");
		case EPCGUtilsDynMeshBoundarySourceElementType::Triangle:
		default: return TEXT("Triangle Region");
		}
	}

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

			const FPCGUtilsDynMeshSelectionDomain SourceDomain = MakeSourceDomain(Factory->SourceElementType);
			if (!Factory->RegionFactory->SupportsDomain(SourceDomain))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("UnsupportedSourceDomain", "The boundary factory's child does not support its configured {0} source domain."),
					FText::FromString(GetSourceDomainName(Factory->SourceElementType))), Context);
				return false;
			}

			SourceEvaluationContext = MakeUnique<FPCGUtilsDynMeshSelectionEvaluationContext>(
				InSelectionContext.MeshData, InSelectionContext.Mesh, SourceDomain);
			TSharedPtr<FPCGUtilsDynMeshSelectionOperation> RegionOperation =
				Factory->RegionFactory->CreateOperation(Context);
			if (!RegionOperation || !RegionOperation->Initialize(*SourceEvaluationContext))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ChildInitializationFailed", "Selection Boundary Factory could not initialize its child region operation."),
					Context);
				return false;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			UE::Geometry::FGeometrySelection RegionSelection;
			RegionSelection.InitializeTypes(SourceDomain.ElementType, SourceDomain.TopologyType);
			if (SourceDomain.ElementType == UE::Geometry::EGeometryElementType::Face)
			{
				for (const int32 TriangleID : Mesh.TriangleIndicesItr())
				{
					if (RegionOperation->TestElement(TriangleID))
					{
						RegionSelection.Selection.Add(
							UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
					}
				}
			}
			else if (SourceDomain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				for (const int32 VertexID : Mesh.VertexIndicesItr())
				{
					if (RegionOperation->TestElement(VertexID))
					{
						RegionSelection.Selection.Add(
							UE::Geometry::FGeoSelectionID::MeshVertex(VertexID).Encoded());
					}
				}
			}
			else
			{
				for (const int32 EdgeID : Mesh.EdgeIndicesItr())
				{
					if (RegionOperation->TestElement(EdgeID))
					{
						PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(
							Mesh, EdgeID, RegionSelection);
					}
				}
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

bool UPCGDynMeshSelectionBoundaryFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		Domain.ElementType == UE::Geometry::EGeometryElementType::Edge && RegionFactory;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshSelectionBoundaryFactoryData::CreateOperationInternal() const
{
	return MakeShared<FSelectionBoundaryOperation>(this);
}

void UPCGDynMeshSelectionBoundaryFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 SourceTypeValue = static_cast<uint8>(SourceElementType);
		bool bExclude = bExcludeMeshBoundaryEdges;
		uint32 ChildCrc = RegionFactory ? RegionFactory->GetOrComputeCrc(true).GetValue() : 0;
		Ar << SourceTypeValue;
		Ar << bExclude;
		Ar << ChildCrc;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Selection Boundary Factory");
}

TArray<FText> UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("BoundaryEdgesAlias", "Boundary Edges Selection Factory"),
		LOCTEXT("OutlineAlias", "Selection Outline Factory")
	};
}

FText UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Evaluates one child factory in the configured source domain, converts its result to a triangle region, and outputs an edge predicate for that region's boundary. Build DynMesh Selection must use the Edge domain.");
}

FString UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return GetSourceDomainName(SourceElementType);
}
#endif

FName UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshSelectionBoundaryFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshSelectionBoundaryFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshSelectionBoundaryFactoryConstants::RegionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshSelectionBoundaryFactoryProviderSettings::CreateFactory(
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
			LOCTEXT("RequiresOneFactory", "Selection Boundary Factory requires exactly one child region factory."),
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
	Factory->SourceElementType = SourceElementType;
	Factory->bExcludeMeshBoundaryEdges = bExcludeMeshBoundaryEdges;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
