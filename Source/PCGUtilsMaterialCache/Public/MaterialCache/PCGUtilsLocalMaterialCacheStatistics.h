#pragma once

#include "CoreMinimal.h"
#include "PCGUtilsLocalMaterialCacheStatistics.generated.h"

/** Snapshot of one local material cache component's activity, for diagnostics and Blueprint display. */
USTRUCT(BlueprintType)
struct PCGUTILSMATERIALCACHE_API FPCGUtilsLocalMaterialCacheStatistics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 NumLocalMaterials = 0;

	/** Number of distinct (owner component, binding name) pairs, ignoring variant. */
	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 NumLocalBindings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 LocalCacheHits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 LocalCacheMisses = 0;

	/** Replacements caused by a changed initialization request for an existing binding. */
	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 Replacements = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 FailedComponentResolutions = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	int32 InvalidEntriesRemoved = 0;

	/** Number of currently-cached local materials grouped by owner component key. */
	UPROPERTY(BlueprintReadOnly, Category = "Local Material Cache Statistics")
	TMap<FName, int32> CountsByOwnerComponent;
};
