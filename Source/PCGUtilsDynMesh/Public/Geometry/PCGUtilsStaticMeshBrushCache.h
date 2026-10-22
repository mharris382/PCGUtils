// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UObject/ObjectKey.h"

class UMaterialInterface;
class UStaticMesh;
struct FStaticMeshBrushProcessing;
struct FStaticMeshBrushCombineSettings;

/**
 * Identifies one processed brush mesh by what it is made from: the Static Mesh asset, the world scale baked into it,
 * and the processing settings. Two brush components with the same inputs share one entry, and a component that a
 * Blueprint construction script re-creates finds its entry again.
 */
struct PCGUTILSDYNMESH_API FStaticMeshBrushMeshKey
{
	TObjectKey<UStaticMesh> Mesh;
	FVector3d Scale = FVector3d::OneVector;
	/** The processing settings exported as text, so equal keys mean equal settings rather than equal hashes. */
	FString ProcessingText;
	uint32 Hash = 0;

	bool operator==(const FStaticMeshBrushMeshKey& Other) const
	{
		return Hash == Other.Hash && Mesh == Other.Mesh && Scale == Other.Scale && ProcessingText == Other.ProcessingText;
	}
	friend uint32 GetTypeHash(const FStaticMeshBrushMeshKey& Key) { return Key.Hash; }
};

/** One brush inside a combined result: its processed mesh and where it sits in the combiner's frame. */
struct PCGUTILSDYNMESH_API FStaticMeshBrushCombinedPart
{
	FStaticMeshBrushMeshKey MeshKey;
	FQuat4d Rotation = FQuat4d::Identity;
	FVector3d Translation = FVector3d::ZeroVector;

	bool operator==(const FStaticMeshBrushCombinedPart& Other) const
	{
		return MeshKey == Other.MeshKey && Rotation == Other.Rotation && Translation == Other.Translation;
	}
};

/** Identifies one combined result: every brush in order, where each sits, and the combine settings. */
struct PCGUTILSDYNMESH_API FStaticMeshBrushCombinedKey
{
	TArray<FStaticMeshBrushCombinedPart> Parts;
	FString SettingsText;
	uint32 Hash = 0;

	bool operator==(const FStaticMeshBrushCombinedKey& Other) const
	{
		return Hash == Other.Hash && SettingsText == Other.SettingsText && Parts == Other.Parts;
	}
	friend uint32 GetTypeHash(const FStaticMeshBrushCombinedKey& Key) { return Key.Hash; }
};

/** A cached mesh. Material IDs index Materials. Immutable once stored; callers copy Mesh before changing it. */
struct PCGUTILSDYNMESH_API FStaticMeshBrushCachedMesh
{
	UE::Geometry::FDynamicMesh3 Mesh;
	TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
	/** Problems found while building. Kept so a cache hit reports them again instead of hiding them. */
	TArray<FText> Warnings;

	TArray<UMaterialInterface*> ResolveMaterials() const;
};

/**
 * Editor-session cache of processed Static Mesh Brush meshes and of combined (self-unioned) brush sets.
 *
 * Entries are keyed by content, not by component, and are dropped when the Static Mesh asset they read reports a
 * change (reimport, edit, rebuild). Least recently used entries are evicted past
 * pcgutils.StaticMeshBrushCache.MaxEntries. Nothing is saved to disk. Game thread only.
 */
class PCGUTILSDYNMESH_API FStaticMeshBrushCache
{
public:
	static FStaticMeshBrushCache& Get();

	static FStaticMeshBrushMeshKey MakeMeshKey(const UStaticMesh* StaticMesh, const FVector& Scale,
		const FStaticMeshBrushProcessing& Processing);
	static FString MakeCombineSettingsText(const FStaticMeshBrushCombineSettings& Settings);
	static void FinalizeCombinedKey(FStaticMeshBrushCombinedKey& Key);

	/**
	 * Returns the processed mesh for a brush: the Static Mesh read at the requested LOD, scaled by Scale, then
	 * simplified when asked. Builds and stores it on a miss. Returns null and sets OutError when the mesh could
	 * not be read.
	 */
	TSharedPtr<const FStaticMeshBrushCachedMesh> FindOrBuildBrushMesh(UStaticMesh* StaticMesh, const FVector& Scale,
		const FStaticMeshBrushProcessing& Processing, FText& OutError);

	TSharedPtr<const FStaticMeshBrushCachedMesh> FindCombined(const FStaticMeshBrushCombinedKey& Key);

	/** Stores a combined result. Dependencies are the Static Meshes it was built from; a change to any of them drops it. */
	void StoreCombined(const FStaticMeshBrushCombinedKey& Key, TSharedPtr<const FStaticMeshBrushCachedMesh> Result,
		const TArray<UStaticMesh*>& Dependencies);

	/** Drops every brush and combined entry built from StaticMesh, then broadcasts OnInvalidated. */
	void InvalidateMesh(const UStaticMesh* StaticMesh);

	/** Drops everything, then broadcasts OnInvalidated with null. */
	void Clear();

	int32 NumBrushEntries() const;
	int32 NumCombinedEntries() const;

	/** Fired after entries are dropped. The mesh is the asset that changed, or null when everything was cleared. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInvalidated, const UStaticMesh*);
	FOnInvalidated OnInvalidated;

private:
	struct FBrushEntry
	{
		TSharedPtr<const FStaticMeshBrushCachedMesh> Result;
		uint64 LastUsed = 0;
	};
	struct FCombinedEntry
	{
		TSharedPtr<const FStaticMeshBrushCachedMesh> Result;
		TArray<TObjectKey<UStaticMesh>> Dependencies;
		uint64 LastUsed = 0;
	};

	TSharedPtr<FStaticMeshBrushCachedMesh> BuildBrushMesh(UStaticMesh* StaticMesh, const FVector& Scale,
		const FStaticMeshBrushProcessing& Processing, FText& OutError) const;
	void WatchMesh(UStaticMesh* StaticMesh);
	void HandleMeshChanged(TObjectKey<UStaticMesh> StaticMesh);
	void DropEntriesFor(TObjectKey<UStaticMesh> StaticMesh);
	void EvictIfNeeded();

	mutable FCriticalSection Lock;
	TMap<FStaticMeshBrushMeshKey, FBrushEntry> BrushEntries;
	TMap<FStaticMeshBrushCombinedKey, FCombinedEntry> CombinedEntries;
	TMap<TObjectKey<UStaticMesh>, TPair<TWeakObjectPtr<UStaticMesh>, FDelegateHandle>> WatchedMeshes;
	uint64 UseCounter = 0;
};
