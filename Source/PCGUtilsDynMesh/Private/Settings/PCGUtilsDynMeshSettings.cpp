// Copyright Max Harris

#include "Settings/PCGUtilsDynMeshSettings.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSettings"

UPCGUtilsDynMeshSettings::UPCGUtilsDynMeshSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("PCGUtilsDynMesh");
}

FName UPCGUtilsDynMeshSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
#if WITH_EDITOR
FText UPCGUtilsDynMeshSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "PCG Utils DynMesh");
}
#endif
#undef LOCTEXT_NAMESPACE
