// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

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
