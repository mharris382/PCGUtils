// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Geometry/PCGUtilsStaticMeshBrushCache.h"
#include "Types/StaticMeshBrushTypes.h"
#include "StaticMeshBrushComponent.generated.h"

/**
 * An editor-only Static Mesh used as a hand-placed brush for PCG. DynMesh | Get Static Mesh Brush Data reads it.
 *
 * The mesh PCG receives is prepared once and cached outside the graph: the Static Mesh read at Processing's LOD,
 * this component's world scale baked in, then simplified if asked. It is rebuilt only when one of those inputs
 * changes (a different asset, an edit to the asset, a different scale, different Processing settings). Moving or
 * rotating the brush does not rebuild it; the cached mesh is placed at read time.
 *
 * Add a Static Mesh Brush Combiner Component to the same actor to merge all of its brushes into one solid.
 */
UCLASS(ClassGroup=(PCGUtils), meta=(BlueprintSpawnableComponent))
class PCGUTILSDYNMESH_API UStaticMeshBrushComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshBrushComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditUndo() override;
#endif

	/** How the Static Mesh is prepared for PCG. Any change here rebuilds this brush's cached mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Processing", meta=(ShowOnlyInnerProperties))
	FStaticMeshBrushProcessing Processing;

	/**
	 * This brush's processed mesh, from the shared cache, built on the first request. Its geometry has the world
	 * scale baked in; place it with GetBrushPlacement(). Returns null and sets OutError when the mesh cannot be read.
	 */
	TSharedPtr<const FStaticMeshBrushCachedMesh> GetProcessedMesh(FText& OutError) const;

	/** Where the processed mesh sits in the world: this component's world rotation and location, without scale. */
	FTransform GetBrushPlacement() const;

	/** The cache key for this brush's current asset, scale and Processing settings. */
	FStaticMeshBrushMeshKey MakeProcessedMeshKey() const;

	/** Drops the cached result for this brush's Static Mesh (every brush using that asset) and builds it again. */
	UFUNCTION(CallInEditor, Category="Brush|Processing")
	void RebuildProcessedMesh();

private:
	void ApplyBrushDefaults();
	void NotifyCombiner() const;
};
