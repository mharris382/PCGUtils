#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsDataCacheTypes.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDataCache : uint8
{
	Offline,
	Runtime
};

UENUM(BlueprintType)
enum class EPCGUtilsDataCacheReferenceOutput : uint8
{
	PointData UMETA(DisplayName="Point Data"),
	AttributeSet UMETA(DisplayName="Attribute Set")
};

namespace PCGUtilsDataCache
{
	PCGUTILSDATACACHE_API extern const FName CacheAssetPathAttribute;
}
