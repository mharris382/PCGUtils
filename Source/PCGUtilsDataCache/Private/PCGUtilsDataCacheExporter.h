#pragma once

#include "CoreMinimal.h"
#include "Elements/IO/PCGSaveAssetElement.h"
#include "PCGUtilsDataCacheExporter.generated.h"

UCLASS()
class UPCGUtilsDataCacheExporter final : public UPCGDataCollectionExporter
{
	GENERATED_BODY()

protected:
	virtual bool ExportAsset(const FString& PackageName, UPCGDataAsset* Asset) override;
};
