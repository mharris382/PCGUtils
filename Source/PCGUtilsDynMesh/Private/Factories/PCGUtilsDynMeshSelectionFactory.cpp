// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

namespace
{
	class FLiteralSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FLiteralSelectionOperation(const UPCGUtilsDynMeshLiteralSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !Factory->SelectionData || !InSelectionContext.MeshData ||
				Factory->SelectionData->GetSourceMeshData() != InSelectionContext.MeshData)
			{
				return false;
			}

			UE::Geometry::FGeometrySelection ConvertedSelection;
			if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
				InSelectionContext.MeshData, InSelectionContext.Mesh,
				Factory->SelectionData->GetSelection(), InSelectionContext.Domain.ElementType,
				Factory->bAllowPartialInclusion, ConvertedSelection))
			{
				return false;
			}

			FGeometryScriptMeshSelection ScriptSelection;
			ScriptSelection.SetSelection(ConvertedSelection);
			TArray<int32> ElementIDs;
			const EGeometryScriptIndexType IndexType =
				PCGUtilsDynMeshSelectionDomains::ToScriptIndexType(InSelectionContext.Domain.ElementType);
			if (ScriptSelection.ConvertToMeshIndexArray(
				InSelectionContext.Mesh, ElementIDs, IndexType) != IndexType)
			{
				return false;
			}

			SelectedElementIDs.Append(ElementIDs);
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return SelectedElementIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGUtilsDynMeshLiteralSelectionFactoryData> Factory;
		TSet<int32> SelectedElementIDs;
	};
}

PCG_DEFINE_TYPE_INFO(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo, UPCGUtilsDynMeshSelectionFactoryData)

bool UPCGUtilsDynMeshSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGUtilsDynMeshSelectionFactoryData::CreateOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> Operation = CreateOperationInternal();
	if (Operation)
	{
		Operation->BindContext(InContext);
	}
	return Operation;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGUtilsDynMeshSelectionFactoryData::CreateOperationInternal() const
{
	return nullptr;
}

bool UPCGUtilsDynMeshLiteralSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGUtilsDynMeshLiteralSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FLiteralSelectionOperation>(this);
}

void UPCGUtilsDynMeshLiteralSelectionFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint32 SelectionCrc = SelectionData ? SelectionData->GetOrComputeCrc(true).GetValue() : 0;
		bool bAllowPartial = bAllowPartialInclusion;
		Ar << SelectionCrc;
		Ar << bAllowPartial;
	}
}

bool FPCGUtilsDynMeshSelectionOperation::Initialize(
	const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext)
{
	SelectionContext = &InSelectionContext;
	return true;
}

namespace PCGUtilsDynMeshFactories
{
	const TSet<FPCGDataTypeBaseId>& GetSelectionFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()};
		return Types;
	}
}

void PCGUtilsDynMeshSelectionFactories::SortByPriority(
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>>& Factories)
{
	Factories.StableSort([](const UPCGUtilsDynMeshSelectionFactoryData& A,
		const UPCGUtilsDynMeshSelectionFactoryData& B)
	{
		return A.Priority > B.Priority;
	});
}

bool PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
	const UPCGUtilsDynMeshSelectionFactoryData* Factory,
	const FPCGUtilsDynMeshSelectionEvaluationContext& EvaluationContext,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection)
{
	using namespace UE::Geometry;
	if (!Factory || !Factory->SupportsDomain(EvaluationContext.Domain))
	{
		return false;
	}

	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> Operation = Factory->CreateOperation(Context);
	if (!Operation || !Operation->Initialize(EvaluationContext))
	{
		return false;
	}

	const FDynamicMesh3& Mesh = EvaluationContext.Mesh;
	const EGeometryElementType ElementType = EvaluationContext.Domain.ElementType;
	OutSelection.InitializeTypes(ElementType, EvaluationContext.Domain.TopologyType);
	if (ElementType == EGeometryElementType::Vertex)
	{
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			if (Operation->TestElement(VertexID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
		}
		return true;
	}

	if (ElementType == EGeometryElementType::Edge)
	{
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			if (Operation->TestElement(EdgeID))
			{
				Mesh.EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&OutSelection](FMeshTriEdgeID TriEdgeID)
				{
					OutSelection.Selection.Add(FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
				});
			}
		}
		return true;
	}

	if (ElementType == EGeometryElementType::Face)
	{
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (Operation->TestElement(TriangleID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}
		return true;
	}

	return false;
}
