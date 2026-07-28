#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MaterialCache/PCGUtilsMaterialVariantKey.h"
#include "MaterialCache/PCGUtilsMaterialVariantRequest.h"
#include "MaterialCache/PCGUtilsMaterialVariantResolveResult.h"
#include "MaterialCache/PCGUtilsMaterialVariantCacheStatistics.h"
#include "PCGUtilsMaterialVariantCacheSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * World-scoped shared cache of material variants: (exact parent material + parameter
 * overrides) -> a reusable UMaterialInterface.
 *
 * Cached variants are treated as immutable once created - no code in this module mutates
 * a cached material after it has been entered into the cache under a key. Callers must
 * likewise never mutate a material returned by ResolveMaterialVariant: request a new
 * variant instead, or use CreateUniqueDynamicMaterialInstance for a genuinely unique,
 * mutable instance that deliberately bypasses the shared cache.
 *
 * Scoped per UWorld so editor worlds, PIE worlds, and packaged runtime worlds never share
 * cached variants, and every cached material has a well-defined, GC-visible owner.
 *
 * All public entry points must be called from the game thread.
 */
UCLASS()
class PCGUTILSMATERIALCACHE_API UPCGUtilsMaterialVariantCacheSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem interface
	virtual void Deinitialize() override;
	//~ End UWorldSubsystem interface

	/**
	 * Resolves a shared, immutable material variant for the given request. Returns an
	 * existing cached material on a hit, or creates and caches a new one on a miss.
	 *
	 * A request with no parameter overrides resolves directly to the (already immutable,
	 * already shared) parent material asset - no dynamic material instance is created for
	 * that case.
	 *
	 * A failed request (null/unresolvable parent, invalid parameter values, ambiguous
	 * duplicate bindings, or a missing/mismatched material parameter) is never entered
	 * into the cache.
	 *
	 * Must be called from the game thread.
	 */
	FPCGUtilsMaterialVariantResolveResult ResolveMaterialVariant(const FPCGUtilsMaterialVariantRequest& Request);

	/**
	 * Creates a uniquely-owned, mutable dynamic material instance. Never added to the
	 * shared cache, and never returned by ResolveMaterialVariant. The caller owns the
	 * mutable semantics of the returned instance and is responsible for keeping it alive.
	 *
	 * Must be called from the game thread.
	 */
	UMaterialInstanceDynamic* CreateUniqueDynamicMaterialInstance(
		UMaterialInterface* ParentMaterial,
		const TArray<FPCGUtilsMaterialParameterOverride>& ParameterOverrides,
		UObject* Outer = nullptr);

	/** Removes all cached variants. Does not mutate or forcibly destroy materials that remain referenced elsewhere. */
	void ClearCache();

	/** Removes all cached variants whose exact requested parent material matches ParentMaterial. Unrelated parents are untouched. */
	void ClearVariantsForParent(const TSoftObjectPtr<UMaterialInterface>& ParentMaterial);

	FPCGUtilsMaterialVariantCacheStatistics GetStatistics() const { return Statistics; }

	/** Writes the current cache statistics to the material cache log category. */
	void LogStatistics() const;

private:
	/** Strongly-referenced (GC-visible) shared variants, owned by this world. */
	UPROPERTY(Transient)
	TMap<FPCGUtilsMaterialVariantKey, TObjectPtr<UMaterialInterface>> CachedVariants;

	UPROPERTY(Transient)
	FPCGUtilsMaterialVariantCacheStatistics Statistics;
};
