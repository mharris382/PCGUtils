// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"

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
