#pragma once

#include "CoreMinimal.h"

class UActorComponent;

/**
 * Centralized helpers for converting between an actor component, its soft object path,
 * and the FName "owner key" used to address it within the local material cache.
 *
 * Shared by UPCGUtilsLocalMaterialCacheComponent, UPCGUtilsMaterialCacheBlueprintLibrary,
 * the Resolve Material Variants PCG node, and tests - component-path parsing and owner-key
 * derivation must not be reimplemented independently in any of those places. A direct
 * component reference and an equivalent soft object path must always produce the same
 * owner key.
 *
 * The owner key is presently just the component's FName. Unreal enforces object-name
 * uniqueness per-Outer, so this is safe among sibling components sharing the same actor
 * Outer, which is the only scenario the local material cache supports in V1 (an owner
 * component must belong to the same actor as the cache component that owns it). This key
 * is NOT a globally unique component identifier and must never be treated as one outside
 * the local cache system.
 */
namespace PCGUtilsMaterialCache
{
	/** Derives the local owner key from a direct, non-null component reference. */
	PCGUTILSMATERIALCACHE_API bool TryGetComponentOwnerKey(const UActorComponent* Component, FName& OutOwnerKey, FText* OutError = nullptr);

	/**
	 * Resolves a soft object path to a currently-instanced UActorComponent.
	 *
	 * Uses FSoftObjectPath::ResolveObject() (find-only, never loads/creates) since actor
	 * components are subobjects of an already-instanced actor, not independently loadable
	 * assets - there is nothing to "load" for a component in isolation.
	 */
	PCGUTILSMATERIALCACHE_API bool TryResolveComponent(const FSoftObjectPath& ComponentPath, UActorComponent*& OutComponent, FText* OutError = nullptr);

	/** Resolves ComponentPath and derives the same owner key TryGetComponentOwnerKey(UActorComponent*) would produce for the resolved component. */
	PCGUTILSMATERIALCACHE_API bool TryGetComponentOwnerKey(const FSoftObjectPath& ComponentPath, FName& OutOwnerKey, UActorComponent** OutComponent = nullptr, FText* OutError = nullptr);

	/**
	 * Builds a soft object path for Component and verifies it round-trips back to the same
	 * component via ResolveObject() before reporting success.
	 *
	 * Not every transient or dynamically-created component necessarily has a persistent,
	 * reliably resolvable path (e.g. a component whose outer chain does not currently root
	 * in a resolvable package). This function fails clearly for that case rather than
	 * returning a path that looks valid but will not resolve - callers should fall back to
	 * the direct component-reference API when this fails.
	 */
	PCGUTILSMATERIALCACHE_API bool TryGetComponentSoftObjectPath(const UActorComponent* Component, FSoftObjectPath& OutPath, FText* OutError = nullptr);

	/** Interprets Object as (or resolves it to) a UActorComponent, for callers holding a generic UObject* that PCG metadata resolution may have produced. */
	PCGUTILSMATERIALCACHE_API bool TryGetComponentFromObject(UObject* Object, UActorComponent*& OutComponent, FText* OutError = nullptr);
}
