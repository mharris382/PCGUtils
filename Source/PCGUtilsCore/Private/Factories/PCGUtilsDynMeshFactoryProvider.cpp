// Copyright Max Harris
// Shared factory infrastructure; intentionally independent of mesh modules.
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshFactoryProvider.h"

#include "Factories/PCGUtilsDynMeshFactoryData.h"

UPCGUtilsDynMeshFactoryData* UPCGUtilsDynMeshFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	return InFactory;
}

UPCGUtilsFactoryData* UPCGUtilsDynMeshFactoryProviderSettings::CreateFactoryBase(FPCGContext* InContext) const
{
	return CreateFactory(InContext);
}

const FPCGDataTypeBaseId& UPCGUtilsDynMeshFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshFactoryDataTypeInfo::AsId();
}
