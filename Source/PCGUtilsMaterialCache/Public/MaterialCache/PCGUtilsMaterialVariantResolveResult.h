#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsMaterialVariantResolveResult.generated.h"

class UMaterialInterface;

/**
 * Result of a shared material variant resolution request.
 *
 * Material is shared and must be treated as immutable: it may be the same object handed
 * to many other callers who requested an equivalent variant. Never call SetParameterValue
 * (or any other mutating API) on it. Request a different variant instead, or create a
 * unique mutable instance via CreateUniqueDynamicMaterialInstance.
 */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialVariantResolveResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Result")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Result")
	bool bWasCacheHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Result")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Result")
	FText ErrorMessage;
};
