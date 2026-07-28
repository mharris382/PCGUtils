#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsMaterialVariantCacheStatistics.generated.h"

/** Snapshot of a material variant cache's activity, for diagnostics and Blueprint display. */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialVariantCacheStatistics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 NumCachedVariants = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 TotalResolutionRequests = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 CacheHits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 CacheMisses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 FailedRequests = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	int32 VariantsCreated = 0;

	/** Number of currently-cached variants grouped by their exact requested parent material. */
	UPROPERTY(BlueprintReadOnly, Category = "Material Variant Cache Statistics")
	TMap<FSoftObjectPath, int32> VariantCountsByParent;
};
