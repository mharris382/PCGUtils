#include "PCGUtilsDataCacheBlueprintLibrary.h"

#include "PCGDataAsset.h"
#include "PCGDataCacheComponent.h"
#include "PCGUtilsDataCacheHelpers.h"
#include "Misc/PackageName.h"

UPCGDataCacheComponent* UPCGUtilsDataCacheBlueprintLibrary::GetDataCacheComponent(const AActor* Actor)
{
	bool bMultiple = false;
	UPCGDataCacheComponent* Result = PCGUtilsDataCacheHelpers::FindSingleCacheComponent(Actor, bMultiple);
	return bMultiple ? nullptr : Result;
}

bool UPCGUtilsDataCacheBlueprintLibrary::GetCacheAssetPathFromActor(
	const AActor* Actor, EPCGUtilsDataCache TargetCache, FSoftObjectPath& OutAssetPath)
{
	OutAssetPath.Reset();
	const UPCGDataCacheComponent* Component = GetDataCacheComponent(Actor);
	return Component && Component->GetCacheAssetPath(TargetCache, OutAssetPath);
}

bool UPCGUtilsDataCacheBlueprintLibrary::DoesCacheDataExist(
	const UPCGDataCacheComponent* CacheComponent, EPCGUtilsDataCache TargetCache)
{
	FSoftObjectPath AssetPath;
	if (!CacheComponent || !CacheComponent->GetCacheAssetPath(TargetCache, AssetPath)) return false;
	return AssetPath.ResolveObject() != nullptr || FPackageName::DoesPackageExist(AssetPath.GetLongPackageName());
}

bool UPCGUtilsDataCacheBlueprintLibrary::DoesActorCacheDataExist(
	const AActor* Actor, EPCGUtilsDataCache TargetCache)
{
	return DoesCacheDataExist(GetDataCacheComponent(Actor), TargetCache);
}

bool UPCGUtilsDataCacheBlueprintLibrary::LoadCacheDataAsset(
	const UPCGDataCacheComponent* CacheComponent, EPCGUtilsDataCache TargetCache, UPCGDataAsset*& OutDataAsset)
{
	OutDataAsset = nullptr;
	FSoftObjectPath AssetPath;
	if (!CacheComponent || !CacheComponent->GetCacheAssetPath(TargetCache, AssetPath)) return false;
	OutDataAsset = Cast<UPCGDataAsset>(AssetPath.TryLoad());
	return OutDataAsset != nullptr;
}

bool UPCGUtilsDataCacheBlueprintLibrary::LoadActorCacheDataAsset(
	const AActor* Actor, EPCGUtilsDataCache TargetCache, UPCGDataAsset*& OutDataAsset)
{
	return LoadCacheDataAsset(GetDataCacheComponent(Actor), TargetCache, OutDataAsset);
}

bool UPCGUtilsDataCacheBlueprintLibrary::GetCacheDataCollection(
	const UPCGDataCacheComponent* CacheComponent, EPCGUtilsDataCache TargetCache,
	FPCGDataCollection& OutDataCollection)
{
	OutDataCollection = FPCGDataCollection();
	UPCGDataAsset* DataAsset = nullptr;
	if (!LoadCacheDataAsset(CacheComponent, TargetCache, DataAsset)) return false;
	OutDataCollection = DataAsset->Data;
	return true;
}

bool UPCGUtilsDataCacheBlueprintLibrary::GetActorCacheDataCollection(
	const AActor* Actor, EPCGUtilsDataCache TargetCache, FPCGDataCollection& OutDataCollection)
{
	return GetCacheDataCollection(GetDataCacheComponent(Actor), TargetCache, OutDataCollection);
}
