#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsMaterialCacheScope.generated.h"

/** Which material authority a Resolve Material Variants node resolves through. */
UENUM(BlueprintType)
enum class EPCGUtilsMaterialCacheScope : uint8
{
	/** World-scoped, content-addressed, immutable: canonical request -> shared UMaterialInterface. */
	GlobalShared,

	/** Actor-scoped, owner-addressed, mutable: owner component + binding + variant -> local UMaterialInstanceDynamic. */
	LocalComponent
};
