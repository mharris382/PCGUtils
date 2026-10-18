#pragma once

#include "CoreMinimal.h"
#include "Elements/IO/PCGSaveAssetElement.h"
#include "PCGUtilsDataCacheExporter.generated.h"

UCLASS()
class UPCGUtilsDataCacheExporter final : public UPCGDataCollectionExporter
{
	GENERATED_BODY()

public:
	/** UPCGUtilsDataCacheAsset, so caches holding only DynMesh data can be saved. */
	virtual TSubclassOf<UPCGDataAsset> GetAssetType() const override;

protected:
	virtual bool ExportAsset(const FString& PackageName, UPCGDataAsset* Asset) override;
};
