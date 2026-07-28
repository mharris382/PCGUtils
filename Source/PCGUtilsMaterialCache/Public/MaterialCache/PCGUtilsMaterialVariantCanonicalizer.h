#pragma once

#include "CoreMinimal.h"

struct FPCGUtilsMaterialVariantRequest;
struct FPCGUtilsMaterialVariantKey;

namespace PCGUtilsMaterialCache
{
	/**
	 * Canonicalizes a material variant request into a deterministic, order-independent
	 * cache key.
	 *
	 * Pure data transformation: does not load/resolve the parent material or any texture
	 * references, and does not touch any UObject state, so it is safe to call from any
	 * thread, including PCG worker threads.
	 *
	 * Fails (returns false, OutError populated) if the request contains non-finite scalar
	 * or vector/color components, or ambiguous duplicate target parameter bindings (the
	 * same parameter association + name + index supplied more than once).
	 */
	PCGUTILSMATERIALCACHE_API bool CanonicalizeMaterialVariantRequest(
		const FPCGUtilsMaterialVariantRequest& Request,
		FPCGUtilsMaterialVariantKey& OutKey,
		FText& OutError);
}
