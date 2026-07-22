#include "PCGDataCacheComponent.h"

#include "PCGUtilsDataCacheHelpers.h"
#include "PCGUtilsDataCacheModule.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectSaveContext.h"

UPCGDataCacheComponent::UPCGDataCacheComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UPCGDataCacheComponent::UsesCache(EPCGUtilsDataCache TargetCache) const
{
	return TargetCache == EPCGUtilsDataCache::Runtime ? bUsesRuntimeCache : bUsesOfflineCache;
}

bool UPCGDataCacheComponent::GetCacheAssetPath(
	EPCGUtilsDataCache TargetCache, FSoftObjectPath& OutAssetPath) const
{
	OutAssetPath.Reset();
	if (!UsesCache(TargetCache)) return false;

	FString PackageName;
	FText Error;
	if (!PCGUtilsDataCacheHelpers::BuildAssetPath(TargetCache, CacheSaveName, PackageName, OutAssetPath, Error))
	{
		UE_LOG(LogPCGUtilsDataCache, Warning, TEXT("Unable to resolve cache path for '%s': %s"),
			*GetNameSafe(GetOwner()), *Error.ToString());
		OutAssetPath.Reset();
		return false;
	}
	return true;
}

bool UPCGDataCacheComponent::GetRuntimeCacheAssetPath(FSoftObjectPath& OutAssetPath) const
{
	return GetCacheAssetPath(EPCGUtilsDataCache::Runtime, OutAssetPath);
}

bool UPCGDataCacheComponent::GetOfflineCacheAssetPath(FSoftObjectPath& OutAssetPath) const
{
	return GetCacheAssetPath(EPCGUtilsDataCache::Offline, OutAssetPath);
}

void UPCGDataCacheComponent::ValidateSingleComponent() const
{
	if (!GetOwner()) return;
	TInlineComponentArray<UPCGDataCacheComponent*> Components(GetOwner());
	if (Components.Num() > 1)
	{
		UE_LOG(LogPCGUtilsDataCache, Error,
			TEXT("Actor '%s' has %d PCGDataCacheComponents; exactly one is supported."),
			*GetOwner()->GetName(), Components.Num());
	}
}

void UPCGDataCacheComponent::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	SynchronizeCacheSaveName();
#endif
	ValidateSingleComponent();
}

void UPCGDataCacheComponent::PostLoad()
{
	Super::PostLoad();
}

void UPCGDataCacheComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
}

#if WITH_EDITOR
bool UPCGDataCacheComponent::SynchronizeCacheSaveName()
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject)) return false;
	const FGuid& ActorGuid = Owner->GetActorGuid();
	if (!ActorGuid.IsValid())
	{
		UE_LOG(LogPCGUtilsDataCache, Error,
			TEXT("Cannot synchronize cache name for actor '%s': persistent Actor GUID is invalid."), *Owner->GetName());
		return false;
	}

	const FName ExpectedName(*FString::Printf(TEXT("PCGCache_%s"), *ActorGuid.ToString(EGuidFormats::Digits)));
	if (CacheSaveName != ExpectedName)
	{
		CacheSaveName = ExpectedName;
	}
	return true;
}

void UPCGDataCacheComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	SynchronizeCacheSaveName();
	Super::PreSave(SaveContext);
}

void UPCGDataCacheComponent::PostEditImport()
{
	Super::PostEditImport();
	SynchronizeCacheSaveName();
}
#endif
