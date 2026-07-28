#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "MaterialCache/PCGUtilsMaterialParameterTypes.h"
#include "PCGUtilsMaterialParameterBinding.generated.h"

/**
 * Maps a PCG source selector (e.g. @Data.Roughness or @Points.Tint) to a target material
 * parameter, for use by the Resolve Material Variants node.
 */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialParameterBinding
{
	GENERATED_BODY()

	/** Where to read the value from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FPCGAttributePropertyInputSelector Source;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FMaterialParameterInfo TargetParameter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EPCGUtilsMaterialParameterType Type = EPCGUtilsMaterialParameterType::Scalar;
};
