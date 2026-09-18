// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "PCGContext.h"

#include "PCGLoadChaosCacheAsset.generated.h"

class UChaosCacheCollection;
class UGeometryCollection;

namespace PCGLoadChaosCacheAssetConstants
{
	inline const FName CollectionOutputPin = TEXT("GC");
}

/** Which frame the sampled collection is expressed in. */
UENUM(BlueprintType)
enum class EPCGChaosCacheSampleSpace : uint8
{
	/**
	 * The Geometry Collection asset's own local space - the frame GC | From Asset produces, so a sampled state
	 * lines up with an unsimulated import of the same asset.
	 */
	Component,

	/** Relative to the Chaos Cache Manager that made the recording. */
	CacheManager UMETA(DisplayName="Cache Manager"),

	/** The world transform the component had when the recording started. */
	RecordedWorld UMETA(DisplayName="Recorded World"),
};

/**
 * Samples a Chaos Cache recording of a Geometry Collection at one moment and brings that bone state into the
 * graph as GC data.
 *
 * The recording is made outside PCG, the normal way: a Chaos Cache Manager observing a Geometry Collection
 * component, in Record mode, run in PIE or Simulate. That leaves a Chaos Cache Collection asset holding one
 * cache per observed component. This node reads one - or all - of those caches back at a chosen time and emits
 * the rest collection posed exactly as the simulation had it, so a settled pile of rubble, or the instant a wall
 * came apart, becomes ordinary GC data for Bones To Points, To DynMesh, pruning or further fracture.
 *
 * Like GC | From Asset, nothing is modified: the cache and the Geometry Collection asset are read, the collection
 * is deep-copied, and the result is published as a new lineage. The cache holds only transforms; the geometry
 * comes from the rest collection of the component that was recorded, or from Rest Collection Override.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Chaos Cache Collection Load Sample Read Playback Recording Record Simulation Simulated Physics Time Frame Pose Rest State From Asset GC Geometry Collection"))
class PCGUTILSCHAOSCACHE_API UPCGLoadChaosCacheAssetSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("LoadChaosCacheAsset"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual void GetStaticTrackedKeys(
		FPCGSelectionKeyToSettingsMap& OutKeysToSettings,
		TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

	/**
	 * The Chaos Cache Collection asset to read - the asset assigned to the Chaos Cache Manager that recorded the
	 * simulation. Required: no asset is a graph error. Read-only: it is never modified.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", meta=(PCG_Overridable))
	TSoftObjectPtr<UChaosCacheCollection> CacheCollection;

	/**
	 * When to sample the recording. In seconds of cache time - the same time the Cache Manager's Start Time uses -
	 * unless Normalized Time is on.
	 *
	 * The recording spans 0 to its last recorded key. A time outside that range is a warning and is clamped to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", meta=(PCG_Overridable))
	float SampleTime = 0.0f;

	/**
	 * Read Sample Time as a fraction of the recording instead of seconds: 0 is the start, 1 the final recorded
	 * state. Useful when the same graph samples recordings of different lengths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", meta=(PCG_Overridable))
	bool bNormalizedTime = false;

	/**
	 * Which cache in the collection to sample, by the name the Cache Manager gave it (its Observed Component's
	 * Cache Name). Leave it as None to sample every Geometry Collection cache in the collection, one GC output
	 * each. Every output is tagged with its cache's name either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", meta=(PCG_Overridable))
	FName CacheName = NAME_None;

	/** Which frame the sampled collection is expressed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", meta=(PCG_Overridable))
	EPCGChaosCacheSampleSpace Space = EPCGChaosCacheSampleSpace::Component;

	/**
	 * Take the geometry from this asset instead of the rest collection of the component that was recorded.
	 *
	 * Needed when the recorded component's collection was transient (spawned at runtime) and so was not saved
	 * with the cache. The asset must share the recorded collection's bone indexing - every recorded track is
	 * checked against its bone count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cache", AdvancedDisplay, meta=(PCG_Overridable))
	TSoftObjectPtr<UGeometryCollection> RestCollectionOverride;

	/** Carry the rest collection's material list onto the GC data, so a round trip back to DynMesh keeps its materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	bool bExtractMaterials = true;

	/**
	 * Keep the geometry of cluster bones whose faces are all invisible - the hidden pre-fracture shapes Fracture
	 * Mode leaves behind. Discarded by default, as GC | From Asset does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", AdvancedDisplay, meta=(PCG_Overridable))
	bool bKeepHiddenGeometry = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug", AdvancedDisplay)
	bool bSynchronousLoad = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGLoadChaosCacheAssetContext : public FPCGContext, public IPCGAsyncLoadingContext {};

class PCGUTILSCHAOSCACHE_API FPCGLoadChaosCacheAssetElement final : public IPCGElement
{
public:
	/** Asset resolution and dynamic tracking both want the game thread; sampling itself does not. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
