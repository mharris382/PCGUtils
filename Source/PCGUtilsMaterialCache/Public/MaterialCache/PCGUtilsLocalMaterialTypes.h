#pragma once

#include "CoreMinimal.h"
#include "MaterialCache/PCGUtilsMaterialVariantKey.h"
#include "PCGUtilsLocalMaterialTypes.generated.h"

class UActorComponent;
class UMaterialInstanceDynamic;

/**
 * Addresses a single locally-owned mutable material: owner component + binding name +
 * optional variant name, scoped within one UPCGUtilsLocalMaterialCacheComponent.
 *
 * NAME_None is a valid VariantName (the "default"/unvaried binding).
 */
USTRUCT()
struct FPCGUtilsLocalMaterialBindingKey
{
	GENERATED_BODY()

	UPROPERTY()
	FName OwnerComponentKey;

	UPROPERTY()
	FName BindingName;

	UPROPERTY()
	FName VariantName = NAME_None;

	bool operator==(const FPCGUtilsLocalMaterialBindingKey& Other) const
	{
		return OwnerComponentKey == Other.OwnerComponentKey
			&& BindingName == Other.BindingName
			&& VariantName == Other.VariantName;
	}

	friend uint32 GetTypeHash(const FPCGUtilsLocalMaterialBindingKey& Key)
	{
		uint32 Hash = GetTypeHash(Key.OwnerComponentKey);
		Hash = HashCombine(Hash, GetTypeHash(Key.BindingName));
		Hash = HashCombine(Hash, GetTypeHash(Key.VariantName));
		return Hash;
	}
};

/**
 * A single locally-owned mutable material entry.
 *
 * InitializationKey is the canonicalized request that produced Material - it represents
 * only how the material was initially created, never its current (possibly
 * gameplay-mutated) parameter values. It is compared against a new resolution's
 * canonicalized request to decide whether the existing MID can be reused as-is.
 */
USTRUCT()
struct FPCGUtilsLocalMaterialEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;

	UPROPERTY()
	FPCGUtilsMaterialVariantKey InitializationKey;

	/** Weak - for diagnostics/validity checks only, never a strong reference. */
	UPROPERTY()
	TWeakObjectPtr<UActorComponent> OwnerComponent;
};
