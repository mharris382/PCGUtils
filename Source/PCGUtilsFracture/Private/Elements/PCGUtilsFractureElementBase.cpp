// Copyright Max Harris

#include "Elements/PCGUtilsFractureElementBase.h"

#include "Factories/PCGUtilsGeometryCollectionFactoryData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFractureElementBase"

#if WITH_EDITOR
FLinearColor UPCGUtilsFractureElementBaseSettings::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(PCGUtilsFracture::DomainColorHex));
}

FLinearColor UPCGUtilsGeometryCollectionFactoryProviderSettings::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(PCGUtilsFracture::DomainColorHex));
}
#endif

UPCGUtilsGeometryCollectionFactoryData* UPCGUtilsGeometryCollectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	return InFactory;
}

UPCGUtilsFactoryData* UPCGUtilsGeometryCollectionFactoryProviderSettings::CreateFactoryBase(FPCGContext* InContext) const
{
	return CreateFactory(InContext);
}

const FPCGDataTypeBaseId& UPCGUtilsGeometryCollectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionFactoryDataTypeInfo::AsId();
}

#undef LOCTEXT_NAMESPACE
