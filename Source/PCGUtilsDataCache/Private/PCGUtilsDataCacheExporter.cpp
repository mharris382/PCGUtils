#include "PCGUtilsDataCacheExporter.h"

#include "PCGDataAsset.h"
#include "PCGUtilsDataCacheAsset.h"

TSubclassOf<UPCGDataAsset> UPCGUtilsDataCacheExporter::GetAssetType() const
{
	return UPCGUtilsDataCacheAsset::StaticClass();
}

bool UPCGUtilsDataCacheExporter::ExportAsset(const FString& PackageName, UPCGDataAsset* Asset)
{
#if WITH_EDITOR
	if (!Asset)
	{
		return false;
	}

	Asset->Data = Data;

#if WITH_EDITORONLY_DATA
	Asset->Description = FText::FromString(AssetDescription);
	Asset->Color = AssetColor;
#endif

	return true;
#else
	return false;
#endif
}
