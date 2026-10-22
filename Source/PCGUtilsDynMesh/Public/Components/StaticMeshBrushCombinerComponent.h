// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Geometry/PCGUtilsStaticMeshBrushCache.h"
#include "Types/StaticMeshBrushTypes.h"
#include "StaticMeshBrushCombinerComponent.generated.h"

class UStaticMesh;
class UStaticMeshBrushComponent;

/**
 * Merges every Static Mesh Brush Component on its actor into one self-unioned solid and caches it, so PCG does
 * not redo the union on every graph run. DynMesh | Get Static Mesh Brush Data reads the combined result (in
 * Automatic or Combined mode) and tags it, so the graph knows no further self union is needed.
 *
 * The cache is keyed by every brush's processed mesh and its place relative to the actor. Moving the whole actor
 * keeps it; moving, rescaling, adding, removing or re-meshing one brush invalidates it.
 */
UCLASS(ClassGroup=(PCGUtils), meta=(BlueprintSpawnableComponent))
class PCGUTILSDYNMESH_API UStaticMeshBrushCombinerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStaticMeshBrushCombinerComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Recombine in the editor as soon as a brush finishes moving, a brush's settings change, or a brush's Static
	 * Mesh is edited, so the result is ready before PCG asks. Off: recombine only when PCG asks and finds the
	 * cached result out of date. Adding or removing a brush is always picked up on the next PCG request.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combine")
	bool bRecombineWhenBrushesChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combine", meta=(ShowOnlyInnerProperties))
	FStaticMeshBrushCombineSettings CombineSettings;

	/** The Static Mesh Brush Components this combiner merges: every one on the owning actor. */
	void GetBrushes(TArray<UStaticMeshBrushComponent*>& OutBrushes) const;

	/**
	 * The combined mesh, from the cache or built now. Its geometry is in the frame given by GetCombinedFrame().
	 * Returns null when there is nothing to combine. OutWarnings lists brushes that were skipped and anything that
	 * went wrong while building.
	 */
	TSharedPtr<const FStaticMeshBrushCachedMesh> GetCombinedMesh(TArray<FText>& OutWarnings) const;

	/** The owning actor's world rotation and location, without scale. The combined mesh is expressed in this frame. */
	FTransform GetCombinedFrame() const;

	/** Called by brushes on this actor when they change. Schedules a recombine on the next editor tick if enabled. */
	void NotifyBrushesChanged();

	/** Rebuilds the combined mesh now, even if a cached one is current. */
	UFUNCTION(CallInEditor, Category="Combine")
	void RecombineNow();

private:
	TSharedPtr<const FStaticMeshBrushCachedMesh> BuildCombined(bool bForceRebuild, TArray<FText>& OutWarnings) const;
	void HandleCacheInvalidated(const UStaticMesh* StaticMesh);
	bool CanRecombineEagerly() const;

	FTSTicker::FDelegateHandle PendingRecombine;
	FDelegateHandle CacheInvalidatedHandle;
};
