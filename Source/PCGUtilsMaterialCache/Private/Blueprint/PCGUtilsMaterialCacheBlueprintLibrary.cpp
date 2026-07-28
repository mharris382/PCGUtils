#include "Blueprint/PCGUtilsMaterialCacheBlueprintLibrary.h"

#include "MaterialCache/PCGUtilsMaterialVariantCacheSubsystem.h"
#include "MaterialCache/PCGUtilsComponentIdentity.h"
#include "MaterialCache/PCGUtilsLocalMaterialCacheLookup.h"
#include "PCGUtilsMaterialCacheModule.h"

#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	UPCGUtilsMaterialVariantCacheSubsystem* GetMaterialCacheSubsystem(UObject* WorldContextObject)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
		return World ? World->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>() : nullptr;
	}
}

FPCGUtilsMaterialVariantResolveResult UPCGUtilsMaterialCacheBlueprintLibrary::ResolveSharedMaterialVariant(
	UObject* WorldContextObject,
	const FPCGUtilsMaterialVariantRequest& Request)
{
	FPCGUtilsMaterialVariantResolveResult Result;
	if (UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = GetMaterialCacheSubsystem(WorldContextObject))
	{
		Result = Subsystem->ResolveMaterialVariant(Request);
	}
	else
	{
		Result.ErrorMessage = NSLOCTEXT("PCGUtilsMaterialCache", "NoSubsystem", "Material variant request failed: no valid world / material cache subsystem.");
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Result.ErrorMessage.ToString());
	}
	return Result;
}

UMaterialInstanceDynamic* UPCGUtilsMaterialCacheBlueprintLibrary::CreateUniqueDynamicMaterialInstance(
	UObject* WorldContextObject,
	UMaterialInterface* ParentMaterial,
	const TArray<FPCGUtilsMaterialParameterOverride>& ParameterOverrides)
{
	if (UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = GetMaterialCacheSubsystem(WorldContextObject))
	{
		return Subsystem->CreateUniqueDynamicMaterialInstance(ParentMaterial, ParameterOverrides);
	}

	UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("CreateUniqueDynamicMaterialInstance failed: no valid world / material cache subsystem."));
	return nullptr;
}

void UPCGUtilsMaterialCacheBlueprintLibrary::ClearMaterialVariantCache(UObject* WorldContextObject)
{
	if (UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = GetMaterialCacheSubsystem(WorldContextObject))
	{
		Subsystem->ClearCache();
	}
}

void UPCGUtilsMaterialCacheBlueprintLibrary::ClearVariantsForParentMaterial(UObject* WorldContextObject, UMaterialInterface* ParentMaterial)
{
	if (UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = GetMaterialCacheSubsystem(WorldContextObject))
	{
		Subsystem->ClearVariantsForParent(TSoftObjectPtr<UMaterialInterface>(ParentMaterial));
	}
}

FPCGUtilsMaterialVariantCacheStatistics UPCGUtilsMaterialCacheBlueprintLibrary::GetMaterialVariantCacheStatistics(UObject* WorldContextObject)
{
	if (UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = GetMaterialCacheSubsystem(WorldContextObject))
	{
		return Subsystem->GetStatistics();
	}
	return FPCGUtilsMaterialVariantCacheStatistics();
}

bool UPCGUtilsMaterialCacheBlueprintLibrary::GetComponentSoftObjectPath(const UActorComponent* Component, FSoftObjectPath& OutComponentPath)
{
	FText Error;
	const bool bSuccess = PCGUtilsMaterialCache::TryGetComponentSoftObjectPath(Component, OutComponentPath, &Error);
	if (!bSuccess)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Error.ToString());
	}
	return bSuccess;
}

bool UPCGUtilsMaterialCacheBlueprintLibrary::GetComponentOwnerKey(const UActorComponent* Component, FName& OutOwnerKey)
{
	FText Error;
	const bool bSuccess = PCGUtilsMaterialCache::TryGetComponentOwnerKey(Component, OutOwnerKey, &Error);
	if (!bSuccess)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Error.ToString());
	}
	return bSuccess;
}

bool UPCGUtilsMaterialCacheBlueprintLibrary::ResolveComponentFromSoftObjectPath(UObject* WorldContextObject, const FSoftObjectPath& ComponentPath, UActorComponent*& OutComponent)
{
	FText Error;
	const bool bSuccess = PCGUtilsMaterialCache::TryResolveComponent(ComponentPath, OutComponent, &Error);
	if (!bSuccess)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Error.ToString());
	}
	return bSuccess;
}

bool UPCGUtilsMaterialCacheBlueprintLibrary::GetComponentOwnerKeyFromSoftObjectPath(UObject* WorldContextObject, const FSoftObjectPath& ComponentPath, FName& OutOwnerKey)
{
	FText Error;
	const bool bSuccess = PCGUtilsMaterialCache::TryGetComponentOwnerKey(ComponentPath, OutOwnerKey, nullptr, &Error);
	if (!bSuccess)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Error.ToString());
	}
	return bSuccess;
}

UPCGUtilsLocalMaterialCacheComponent* UPCGUtilsMaterialCacheBlueprintLibrary::FindLocalMaterialCacheComponent(UObject* Context, FName CacheName)
{
	FText Error;
	UPCGUtilsLocalMaterialCacheComponent* Found = PCGUtilsMaterialCache::FindLocalMaterialCacheComponentFromContext(Context, CacheName, Error);
	if (!Found)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Error.ToString());
	}
	return Found;
}
