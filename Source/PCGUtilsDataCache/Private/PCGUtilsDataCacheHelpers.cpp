#include "PCGUtilsDataCacheHelpers.h"

#include "PCGDataCacheComponent.h"
#include "PCGUtilsDataCacheSettings.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"

bool PCGUtilsDataCacheHelpers::NormalizeAndValidateFolder(
	const FString& InFolder, FString& OutFolder, FText& OutError)
{
	OutFolder = InFolder;
	OutFolder.TrimStartAndEndInline();
	OutFolder.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (OutFolder.Len() > 1 && OutFolder.EndsWith(TEXT("/")))
	{
		OutFolder.LeftChopInline(1);
	}

	if (OutFolder.IsEmpty() || !OutFolder.StartsWith(TEXT("/")))
	{
		OutError = FText::Format(NSLOCTEXT("PCGUtilsDataCache", "FolderMountedRoot",
			"Cache folder '{0}' must begin with a mounted content root such as /Game."), FText::FromString(InFolder));
		return false;
	}

	if (!FPackageName::IsValidLongPackageName(OutFolder, true, &OutError))
	{
		return false;
	}

	const FString MountPoint = FPackageName::GetPackageMountPoint(OutFolder, false).ToString();
	if (MountPoint.IsEmpty() || !FPackageName::MountPointExists(MountPoint))
	{
		OutError = FText::Format(NSLOCTEXT("PCGUtilsDataCache", "FolderUnknownMount",
			"Cache folder '{0}' does not begin with a mounted content root."), FText::FromString(InFolder));
		return false;
	}
	return true;
}

bool PCGUtilsDataCacheHelpers::IsCacheAvailable(EPCGUtilsDataCache TargetCache)
{
#if WITH_EDITOR
	return true;
#else
	return TargetCache == EPCGUtilsDataCache::Runtime;
#endif
}

bool PCGUtilsDataCacheHelpers::BuildAssetPath(
	EPCGUtilsDataCache TargetCache,
	FName CacheSaveName,
	FString& OutPackageName,
	FSoftObjectPath& OutObjectPath,
	FText& OutError)
{
	OutPackageName.Reset();
	OutObjectPath.Reset();
	if (!IsCacheAvailable(TargetCache))
	{
		OutError = NSLOCTEXT("PCGUtilsDataCache", "OfflineUnavailable", "Offline PCG caches are editor-only.");
		return false;
	}
	if (CacheSaveName.IsNone())
	{
		OutError = NSLOCTEXT("PCGUtilsDataCache", "MissingSaveName", "The cache component has no synchronized save name.");
		return false;
	}

	const UPCGUtilsDataCacheSettings* Settings = GetDefault<UPCGUtilsDataCacheSettings>();
	if (!Settings)
	{
		OutError = NSLOCTEXT("PCGUtilsDataCache", "MissingSettings", "PCG data cache project settings are unavailable.");
		return false;
	}

	const FString RawFolder = TargetCache == EPCGUtilsDataCache::Runtime
		? Settings->RuntimeCacheFolder.Path : Settings->OfflineCacheFolder.Path;
	FString Folder;
	if (!NormalizeAndValidateFolder(RawFolder, Folder, OutError))
	{
		return false;
	}

	const FString AssetName = CacheSaveName.ToString();
	OutPackageName = Folder / AssetName;
	FText PackageError;
	if (!FPackageName::IsValidLongPackageName(OutPackageName, true, &PackageError))
	{
		OutError = FText::Format(NSLOCTEXT("PCGUtilsDataCache", "InvalidPackage", "Invalid cache package '{0}': {1}"),
			FText::FromString(OutPackageName), PackageError);
		OutPackageName.Reset();
		return false;
	}

	OutObjectPath = FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *OutPackageName, *AssetName));
	return OutObjectPath.IsValid();
}

UPCGDataCacheComponent* PCGUtilsDataCacheHelpers::FindSingleCacheComponent(
	const AActor* Actor, bool& bOutMultiple)
{
	bOutMultiple = false;
	if (!IsValid(Actor)) return nullptr;
	TInlineComponentArray<UPCGDataCacheComponent*> Components(Actor);
	bOutMultiple = Components.Num() > 1;
	return Components.Num() == 1 ? Components[0] : nullptr;
}
