#pragma once

#include "CoreMinimal.h"

class AActor;
class UActorComponent;
class UPCGUtilsLocalMaterialCacheComponent;

/**
 * Helpers for finding the appropriate UPCGUtilsLocalMaterialCacheComponent on an actor.
 *
 * Never silently creates a cache component, and never silently picks an arbitrary one
 * when the actor has more than one - callers must disambiguate with CacheName in that
 * case, or resolution fails with a diagnostic identifying the ambiguity.
 */
namespace PCGUtilsMaterialCache
{
	/**
	 * Resolution rules:
	 * - CacheName == NAME_None: succeeds only if Actor has exactly one cache component.
	 *   Zero is reported as "no cache component"; more than one is reported as "ambiguous,
	 *   CacheName required".
	 * - CacheName != NAME_None: succeeds only if exactly one cache component on Actor has
	 *   a matching CacheName. Zero matches or duplicate matching CacheName values are both
	 *   reported as errors.
	 */
	PCGUTILSMATERIALCACHE_API UPCGUtilsLocalMaterialCacheComponent* FindLocalMaterialCacheComponentOnActor(AActor* Actor, FName CacheName, FText& OutError);

	/**
	 * Context may be a UPCGUtilsLocalMaterialCacheComponent (used directly, CacheName is
	 * ignored), any other UActorComponent (searches its owning actor), or an AActor
	 * (searches itself). See FindLocalMaterialCacheComponentOnActor for disambiguation
	 * rules when Context is/resolves to an actor.
	 */
	PCGUTILSMATERIALCACHE_API UPCGUtilsLocalMaterialCacheComponent* FindLocalMaterialCacheComponentFromContext(UObject* Context, FName CacheName, FText& OutError);
}
