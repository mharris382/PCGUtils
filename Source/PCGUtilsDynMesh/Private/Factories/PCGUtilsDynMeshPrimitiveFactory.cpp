// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshPrimitiveFactory.h"

#include "PCGContext.h"

PCG_DEFINE_TYPE_INFO(FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo, UPCGUtilsDynMeshPrimitiveFactoryData)

TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> UPCGUtilsDynMeshPrimitiveFactoryData::CreateOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> Operation = CreateOperationInternal();
	if (Operation)
	{
		Operation->BindContext(InContext);
	}
	return Operation;
}

TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> UPCGUtilsDynMeshPrimitiveFactoryData::CreateOperationInternal() const
{
	return nullptr;
}

namespace PCGUtilsDynMeshFactories
{
	const TSet<FPCGDataTypeBaseId>& GetPrimitiveBuilderFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo::AsId()};
		return Types;
	}
}
