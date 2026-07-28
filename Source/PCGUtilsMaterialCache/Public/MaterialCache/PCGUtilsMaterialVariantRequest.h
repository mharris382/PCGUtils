#pragma once

#include "CoreMinimal.h"
#include "MaterialCache/PCGUtilsMaterialParameterTypes.h"
#include "PCGUtilsMaterialVariantRequest.generated.h"

class UMaterialInterface;

/**
 * A request to resolve a shared material variant: an exact parent material plus a set of
 * explicitly supplied parameter overrides.
 *
 * The exact requested parent material is part of the request identity - it is never
 * reduced to a root material. A request with no override for a parameter is a distinct
 * request from one that explicitly sets that parameter to a value equal to its inherited
 * default, even though the two currently produce visually identical results.
 */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialVariantRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Variant Request")
	TSoftObjectPtr<UMaterialInterface> ParentMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Variant Request")
	TArray<FPCGUtilsMaterialParameterOverride> ParameterOverrides;
};
