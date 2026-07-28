#pragma once

#include "CoreMinimal.h"
#include "MaterialCache/PCGUtilsMaterialParameterTypes.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace PCGUtilsMaterialCache
{
	/**
	 * Validates and applies each override to MID using FMaterialParameterInfo-aware APIs.
	 *
	 * Shared by the global material variant cache and the local material cache component
	 * so parameter validation/application logic exists in exactly one place - do not
	 * reimplement this elsewhere.
	 *
	 * Fails atomically: on the first missing/mismatched parameter or unresolved texture,
	 * returns false with OutError populated. Callers must not treat MID as a valid,
	 * fully-initialized result in that case (do not cache/store it under its intended key).
	 *
	 * Must be called from the game thread - queries and mutates live UObject state.
	 */
	PCGUTILSMATERIALCACHE_API bool TryApplyMaterialParameterOverrides(
		UMaterialInstanceDynamic* MID,
		UMaterialInterface* ParentForDiagnostics,
		const TArray<FPCGUtilsMaterialParameterOverride>& Overrides,
		FText& OutError);
}
