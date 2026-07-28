#pragma once

#include "CoreMinimal.h"
#include "MaterialCache/PCGUtilsMaterialParameterTypes.h"
#include "PCGUtilsMaterialVariantKey.generated.h"

/**
 * A single normalized (canonicalized) parameter override inside a material variant key.
 *
 * Internal cache-identity detail, not part of the public request API - always produced by
 * PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest, never hand-built.
 */
USTRUCT()
struct FPCGUtilsMaterialVariantCanonicalOverride
{
	GENERATED_BODY()

	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	uint8 Association = 0;

	UPROPERTY()
	int32 ParameterIndex = INDEX_NONE;

	UPROPERTY()
	EPCGUtilsMaterialParameterType Type = EPCGUtilsMaterialParameterType::Scalar;

	UPROPERTY()
	float ScalarValue = 0.0f;

	UPROPERTY()
	FLinearColor VectorValue = FLinearColor::White;

	UPROPERTY()
	FSoftObjectPath TexturePath;

	bool operator==(const FPCGUtilsMaterialVariantCanonicalOverride& Other) const
	{
		return ParameterName == Other.ParameterName
			&& Association == Other.Association
			&& ParameterIndex == Other.ParameterIndex
			&& Type == Other.Type
			&& ScalarValue == Other.ScalarValue
			&& VectorValue == Other.VectorValue
			&& TexturePath == Other.TexturePath;
	}

	friend uint32 GetTypeHash(const FPCGUtilsMaterialVariantCanonicalOverride& Override)
	{
		uint32 Hash = GetTypeHash(Override.ParameterName);
		Hash = HashCombine(Hash, GetTypeHash(Override.Association));
		Hash = HashCombine(Hash, GetTypeHash(Override.ParameterIndex));
		Hash = HashCombine(Hash, GetTypeHash(Override.Type));
		Hash = HashCombine(Hash, GetTypeHash(Override.ScalarValue));
		Hash = HashCombine(Hash, GetTypeHash(Override.VectorValue.R));
		Hash = HashCombine(Hash, GetTypeHash(Override.VectorValue.G));
		Hash = HashCombine(Hash, GetTypeHash(Override.VectorValue.B));
		Hash = HashCombine(Hash, GetTypeHash(Override.VectorValue.A));
		Hash = HashCombine(Hash, GetTypeHash(Override.TexturePath));
		return Hash;
	}
};

/**
 * Deterministic, order-independent identity for a resolved material variant request.
 *
 * Two requests canonicalize to an equal key if and only if they are semantically the same
 * request: same exact parent material, same explicitly supplied parameter overrides,
 * independent of the order they were supplied in.
 *
 * Stored as a UPROPERTY TMap key (see UPCGUtilsMaterialVariantCacheSubsystem::CachedVariants)
 * so it must remain a plain reflectable USTRUCT.
 */
USTRUCT()
struct PCGUTILSMATERIALCACHE_API FPCGUtilsMaterialVariantKey
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath ParentMaterialPath;

	/** Sorted deterministically so caller-supplied order never affects equality. */
	UPROPERTY()
	TArray<FPCGUtilsMaterialVariantCanonicalOverride> SortedOverrides;

	/** Precomputed combined hash. Equality below still performs a full structural compare - this is not used as identity on its own. */
	UPROPERTY()
	uint32 CachedHash = 0;

	bool operator==(const FPCGUtilsMaterialVariantKey& Other) const
	{
		return CachedHash == Other.CachedHash
			&& ParentMaterialPath == Other.ParentMaterialPath
			&& SortedOverrides == Other.SortedOverrides;
	}

	friend uint32 GetTypeHash(const FPCGUtilsMaterialVariantKey& Key)
	{
		return Key.CachedHash;
	}

	void RecomputeHash()
	{
		CachedHash = GetTypeHash(ParentMaterialPath);
		for (const FPCGUtilsMaterialVariantCanonicalOverride& Override : SortedOverrides)
		{
			CachedHash = HashCombine(CachedHash, GetTypeHash(Override));
		}
	}
};
