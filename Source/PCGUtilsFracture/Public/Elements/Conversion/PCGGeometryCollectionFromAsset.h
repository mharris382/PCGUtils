// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "PCGContext.h"

#include "PCGGeometryCollectionFromAsset.generated.h"

class UGeometryCollection;

namespace PCGGeometryCollectionFromAssetConstants
{
	inline const FName CollectionOutputPin = TEXT("GC");
}

/**
 * Brings an existing Geometry Collection *asset* into a graph as transient, mutable GC data.
 *
 * The counterpart of the engine's Mesh To Dynamic Mesh node, and it works the same way: the asset is immutable
 * and is never touched. Its `FGeometryCollection` is deep-copied on the way in, so everything downstream -
 * fracture, prune, selection - operates on a private copy that lives and dies inside one graph execution. No
 * asset, actor, component or package is created or modified.
 *
 * What this unlocks is re-fracturing work that was authored by hand: fracture a shape in Fracture Mode, save
 * it, then drive further levels, pruning or piece-scattering from PCG. It is also the shortest route to a
 * collection whose hierarchy came from somewhere other than this module, which is why it validates what it
 * imports rather than assuming.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Asset Import Load Get From To GC Geometry Collection Source Reference Existing Fracture"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionFromAssetSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCFromAsset"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual void GetStaticTrackedKeys(
		FPCGSelectionKeyToSettingsMap& OutKeysToSettings,
		TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

	/** The Geometry Collection asset to import. Read-only: its contents are copied, never modified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	TSoftObjectPtr<UGeometryCollection> Asset;

	/** Carry the asset's material list onto the GC data, so a round trip back to DynMesh keeps its materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	bool bExtractMaterials = true;

	/**
	 * Keep the geometry of cluster bones whose faces are all invisible.
	 *
	 * An asset fractured in Fracture Mode carries a full hidden copy of each pre-fracture shape on the bone
	 * that was cut, because Unreal's cutters mark the old faces invisible rather than removing them. Nothing
	 * downstream can use it and every later operation carries it along, so it is discarded by default - the
	 * same choice GC | Fracture makes. Enable this only to inspect what the asset actually contains.
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

struct FPCGGeometryCollectionFromAssetContext : public FPCGContext, public IPCGAsyncLoadingContext {};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionFromAssetElement final : public IPCGElement
{
public:
	/** Asset resolution and dynamic tracking both want the game thread; the copy itself is cheap. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
