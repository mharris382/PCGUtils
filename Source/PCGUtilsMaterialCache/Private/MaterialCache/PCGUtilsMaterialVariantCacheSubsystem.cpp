#include "MaterialCache/PCGUtilsMaterialVariantCacheSubsystem.h"

#include "MaterialCache/PCGUtilsMaterialVariantCanonicalizer.h"
#include "MaterialCache/PCGUtilsMaterialParameterApplication.h"
#include "PCGUtilsMaterialCacheModule.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMaterialVariantCacheSubsystem"

void UPCGUtilsMaterialVariantCacheSubsystem::Deinitialize()
{
	ClearCache();
	Super::Deinitialize();
}

FPCGUtilsMaterialVariantResolveResult UPCGUtilsMaterialVariantCacheSubsystem::ResolveMaterialVariant(const FPCGUtilsMaterialVariantRequest& Request)
{
	checkf(IsInGameThread(), TEXT("UPCGUtilsMaterialVariantCacheSubsystem::ResolveMaterialVariant must be called from the game thread."));

	FPCGUtilsMaterialVariantResolveResult Result;
	Statistics.TotalResolutionRequests++;

	UMaterialInterface* ParentMaterial = Request.ParentMaterial.LoadSynchronous();
	if (!ParentMaterial)
	{
		Result.ErrorMessage = FText::Format(
			LOCTEXT("NullParent", "Material variant request failed: parent material '{0}' could not be resolved/loaded."),
			FText::FromString(Request.ParentMaterial.ToSoftObjectPath().ToString()));
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Result.ErrorMessage.ToString());
		Statistics.FailedRequests++;
		return Result;
	}

	FPCGUtilsMaterialVariantKey Key;
	FText CanonicalizeError;
	if (!PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(Request, Key, CanonicalizeError))
	{
		Result.ErrorMessage = CanonicalizeError;
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Result.ErrorMessage.ToString());
		Statistics.FailedRequests++;
		return Result;
	}

	if (const TObjectPtr<UMaterialInterface>* Existing = CachedVariants.Find(Key))
	{
		Result.Material = *Existing;
		Result.bSucceeded = true;
		Result.bWasCacheHit = true;
		Statistics.CacheHits++;
		return Result;
	}

	Statistics.CacheMisses++;

	if (Key.SortedOverrides.Num() == 0)
	{
		// No overrides requested: the parent material asset itself already satisfies the
		// request and is already immutable/shared, so there's no need to allocate a MID.
		CachedVariants.Add(Key, ParentMaterial);
		Statistics.NumCachedVariants = CachedVariants.Num();
		Statistics.VariantsCreated++;
		Statistics.VariantCountsByParent.FindOrAdd(Key.ParentMaterialPath)++;

		Result.Material = ParentMaterial;
		Result.bSucceeded = true;
		Result.bWasCacheHit = false;
		return Result;
	}

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(ParentMaterial, this);
	if (!NewMID)
	{
		Result.ErrorMessage = LOCTEXT("MIDCreateFailed", "Material variant request failed: could not create a dynamic material instance for the requested parent material.");
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Result.ErrorMessage.ToString());
		Statistics.FailedRequests++;
		return Result;
	}

	FText ApplyError;
	if (!PCGUtilsMaterialCache::TryApplyMaterialParameterOverrides(NewMID, ParentMaterial, Request.ParameterOverrides, ApplyError))
	{
		// Never cache a partially-applied result - the key would misleadingly claim
		// values that were not actually applied.
		Result.ErrorMessage = ApplyError;
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *Result.ErrorMessage.ToString());
		Statistics.FailedRequests++;
		return Result;
	}

	CachedVariants.Add(Key, NewMID);
	Statistics.NumCachedVariants = CachedVariants.Num();
	Statistics.VariantsCreated++;
	Statistics.VariantCountsByParent.FindOrAdd(Key.ParentMaterialPath)++;

	Result.Material = NewMID;
	Result.bSucceeded = true;
	Result.bWasCacheHit = false;
	return Result;
}

UMaterialInstanceDynamic* UPCGUtilsMaterialVariantCacheSubsystem::CreateUniqueDynamicMaterialInstance(
	UMaterialInterface* ParentMaterial,
	const TArray<FPCGUtilsMaterialParameterOverride>& ParameterOverrides,
	UObject* Outer)
{
	checkf(IsInGameThread(), TEXT("UPCGUtilsMaterialVariantCacheSubsystem::CreateUniqueDynamicMaterialInstance must be called from the game thread."));

	if (!ParentMaterial)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("CreateUniqueDynamicMaterialInstance failed: parent material is null."));
		return nullptr;
	}

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(ParentMaterial, Outer ? Outer : this);
	if (!NewMID)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("CreateUniqueDynamicMaterialInstance failed: could not create a dynamic material instance."));
		return nullptr;
	}

	// This instance is uniquely owned by the caller (never entered into CachedVariants),
	// so - unlike a shared resolution - a partially-applied override list here cannot
	// corrupt any other caller. Log a warning and still hand back the instance.
	FText ApplyError;
	if (!PCGUtilsMaterialCache::TryApplyMaterialParameterOverrides(NewMID, ParentMaterial, ParameterOverrides, ApplyError))
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *ApplyError.ToString());
	}

	return NewMID;
}

void UPCGUtilsMaterialVariantCacheSubsystem::ClearCache()
{
	CachedVariants.Empty();
	Statistics.NumCachedVariants = 0;
	Statistics.VariantCountsByParent.Empty();
}

void UPCGUtilsMaterialVariantCacheSubsystem::ClearVariantsForParent(const TSoftObjectPtr<UMaterialInterface>& ParentMaterial)
{
	const FSoftObjectPath TargetPath = ParentMaterial.ToSoftObjectPath();

	for (auto It = CachedVariants.CreateIterator(); It; ++It)
	{
		if (It.Key().ParentMaterialPath == TargetPath)
		{
			It.RemoveCurrent();
		}
	}

	Statistics.NumCachedVariants = CachedVariants.Num();
	Statistics.VariantCountsByParent.Remove(TargetPath);
}

void UPCGUtilsMaterialVariantCacheSubsystem::LogStatistics() const
{
	UE_LOG(LogPCGUtilsMaterialCache, Log,
		TEXT("Material Variant Cache Statistics: Cached=%d TotalRequests=%d Hits=%d Misses=%d Failed=%d Created=%d"),
		Statistics.NumCachedVariants,
		Statistics.TotalResolutionRequests,
		Statistics.CacheHits,
		Statistics.CacheMisses,
		Statistics.FailedRequests,
		Statistics.VariantsCreated);
}

#undef LOCTEXT_NAMESPACE
