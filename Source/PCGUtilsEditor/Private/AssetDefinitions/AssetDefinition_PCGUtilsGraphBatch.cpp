#include "AssetDefinitions/AssetDefinition_PCGUtilsGraphBatch.h"

#include "Data/PCGUtilsGraphBatch.h"
#include "PCGEditorCommon.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_PCGUtilsGraphBatch)

#define LOCTEXT_NAMESPACE "AssetDefinition_PCGUtilsGraphBatch"

FText UAssetDefinition_PCGUtilsGraphBatch::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "PCG Graph Batch");
}

FLinearColor UAssetDefinition_PCGUtilsGraphBatch::GetAssetColor() const
{
	return FColor::Turquoise;
}

TSoftClassPtr<UObject> UAssetDefinition_PCGUtilsGraphBatch::GetAssetClass() const
{
	return UPCGUtilsGraphBatch::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_PCGUtilsGraphBatch::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = {FPCGEditorCommon::PCGAssetSubCategoryPath};
	return Categories;
}

#undef LOCTEXT_NAMESPACE
