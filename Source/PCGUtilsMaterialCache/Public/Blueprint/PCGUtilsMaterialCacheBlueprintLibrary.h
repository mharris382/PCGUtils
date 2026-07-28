#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MaterialCache/PCGUtilsMaterialVariantRequest.h"
#include "MaterialCache/PCGUtilsMaterialVariantResolveResult.h"
#include "MaterialCache/PCGUtilsMaterialVariantCacheStatistics.h"
#include "PCGUtilsMaterialCacheBlueprintLibrary.generated.h"

class UActorComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPCGUtilsLocalMaterialCacheComponent;

/**
 * Blueprint entry points for the shared, per-world material variant cache.
 *
 * IMPORTANT: the material returned by Resolve Shared Material Variant is shared and must
 * be treated as immutable. Do not call Set Scalar/Vector/Texture Parameter Value on it -
 * doing so would silently corrupt every other actor/point currently using that cached
 * variant. If parameter values change, issue another Resolve Shared Material Variant
 * request instead. If you need a material only you own and can freely mutate, use
 * Create Unique Dynamic Material Instance - it always bypasses the shared cache.
 */
UCLASS()
class PCGUTILSMATERIALCACHE_API UPCGUtilsMaterialCacheBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolves a shared, immutable material variant. Repeated requests with an equivalent
	 * parent material and parameter overrides return the same cached material object.
	 *
	 * Do not mutate the returned material - request another variant instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache", meta = (WorldContext = "WorldContextObject"))
	static FPCGUtilsMaterialVariantResolveResult ResolveSharedMaterialVariant(
		UObject* WorldContextObject,
		const FPCGUtilsMaterialVariantRequest& Request);

	/**
	 * Creates a uniquely-owned, mutable dynamic material instance. Never shared and never
	 * added to the cache. The caller owns the mutable semantics of the returned instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache", meta = (WorldContext = "WorldContextObject"))
	static UMaterialInstanceDynamic* CreateUniqueDynamicMaterialInstance(
		UObject* WorldContextObject,
		UMaterialInterface* ParentMaterial,
		const TArray<FPCGUtilsMaterialParameterOverride>& ParameterOverrides);

	/** Removes all cached material variants for this world. Does not mutate materials that remain referenced elsewhere. */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache", meta = (WorldContext = "WorldContextObject"))
	static void ClearMaterialVariantCache(UObject* WorldContextObject);

	/** Removes all cached variants whose exact requested parent material matches ParentMaterial. Unrelated parents are untouched. */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache", meta = (WorldContext = "WorldContextObject"))
	static void ClearVariantsForParentMaterial(UObject* WorldContextObject, UMaterialInterface* ParentMaterial);

	/** Returns a snapshot of the cache's current statistics. */
	UFUNCTION(BlueprintPure, Category = "PCG Utils|Material Cache", meta = (WorldContext = "WorldContextObject"))
	static FPCGUtilsMaterialVariantCacheStatistics GetMaterialVariantCacheStatistics(UObject* WorldContextObject);

	// ── Component identity helpers (shared by the local material cache, PCG node, and callers) ──
	//
	// The owner-key FName these helpers produce/consume is meaningful only within the local
	// material cache system - it is not a globally unique component identifier.

	/** Builds a soft object path for Component. Fails for components without a persistently resolvable path (see UPCGUtilsLocalMaterialCacheComponent docs) - use the direct-component API in that case. */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache|Local", meta = (DisplayName = "Get Component Soft Object Path"))
	static bool GetComponentSoftObjectPath(const UActorComponent* Component, FSoftObjectPath& OutComponentPath);

	/** Derives the local cache owner key for Component. */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache|Local", meta = (DisplayName = "Get Component Owner Key"))
	static bool GetComponentOwnerKey(const UActorComponent* Component, FName& OutOwnerKey);

	/** Resolves ComponentPath to a currently-instanced UActorComponent (find-only, never loads). */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache|Local", meta = (WorldContext = "WorldContextObject", DisplayName = "Resolve Component from Soft Object Path"))
	static bool ResolveComponentFromSoftObjectPath(UObject* WorldContextObject, const FSoftObjectPath& ComponentPath, UActorComponent*& OutComponent);

	/** Resolves ComponentPath and derives the same owner key GetComponentOwnerKey would produce for the resolved component. */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache|Local", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Component Owner Key from Soft Object Path"))
	static bool GetComponentOwnerKeyFromSoftObjectPath(UObject* WorldContextObject, const FSoftObjectPath& ComponentPath, FName& OutOwnerKey);

	/**
	 * Finds the local material cache component from Context: used directly if Context
	 * already is one, otherwise searches Context's owning actor (UActorComponent) or
	 * Context itself (AActor). Never creates one. Fails if none exists, or if more than
	 * one exists and CacheName does not uniquely disambiguate.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Material Cache|Local", meta = (DisplayName = "Find Local Material Cache Component"))
	static UPCGUtilsLocalMaterialCacheComponent* FindLocalMaterialCacheComponent(UObject* Context, FName CacheName);
};
