#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialParameters.h"
#include "PCGUtilsMaterialParameterTypes.generated.h"

class UTexture;

/**
 * The kinds of material parameter overrides V1 of the material cache can apply.
 *
 * Limited to what UMaterialInstanceDynamic can apply reliably at runtime. Static
 * switches and other static-layer parameters are intentionally out of scope.
 */
UENUM(BlueprintType)
enum class EPCGUtilsMaterialParameterType : uint8
{
	Scalar,
	Vector,
	Texture
};

/**
 * A single dynamic material parameter override.
 *
 * Uses FMaterialParameterInfo (not only FName) to identify the target parameter so the
 * design does not unnecessarily prevent future material-layer parameter support.
 */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialParameterOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Parameter")
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Parameter")
	EPCGUtilsMaterialParameterType Type = EPCGUtilsMaterialParameterType::Scalar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Parameter", meta = (EditCondition = "Type == EPCGUtilsMaterialParameterType::Scalar", EditConditionHides))
	float ScalarValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Parameter", meta = (EditCondition = "Type == EPCGUtilsMaterialParameterType::Vector", EditConditionHides))
	FLinearColor VectorValue = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Parameter", meta = (EditCondition = "Type == EPCGUtilsMaterialParameterType::Texture", EditConditionHides))
	TSoftObjectPtr<UTexture> TextureValue;
};
