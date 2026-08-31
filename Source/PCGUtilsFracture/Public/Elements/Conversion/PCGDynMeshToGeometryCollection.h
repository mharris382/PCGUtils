// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGDynMeshToGeometryCollection.generated.h"

namespace PCGDynMeshToGeometryCollectionConstants
{
	inline const FName MeshInputPin = TEXT("DynMesh");
	inline const FName CollectionOutputPin = TEXT("GC");
}

/**
 * Builds a transient Geometry Collection from one or more DynMeshes so the Fracture backend can be used as a
 * procedural modelling step. No asset, actor or component is created.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection Mesh To Collection Fracture GC"))
class PCGUTILSFRACTURE_API UPCGDynMeshToGeometryCollectionSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshToGC"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Merge every DynMesh on the pin into one collection, each becoming its own bone under a shared root.
	 * When disabled, each input DynMesh produces its own single-bone collection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collection", meta=(PCG_Overridable))
	bool bMergeInputsIntoOneCollection = true;

	/**
	 * Split each DynMesh into one bone per connected component. Useful when a mesh already contains separate
	 * shells; prefer feeding separate DynMeshes when you already have them apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands", meta=(PCG_Overridable))
	bool bSplitIslands = false;

	/** Treat coincident vertices as connected even when the topology does not join them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands",
		meta=(PCG_Overridable, EditCondition="bSplitIslands"))
	bool bConnectIslandsByVertexOverlap = false;

	/** Vertices closer than this are considered overlapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands",
		meta=(PCG_Overridable, EditCondition="bSplitIslands", Units="cm", ClampMin="0.0"))
	float ConnectVerticesThreshold = 0.001f;

	/** If > 0, bridge islands whose surfaces are within this vertex-to-triangle distance. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands",
		meta=(PCG_Overridable, EditCondition="bSplitIslands", Units="cm", ClampMin="0.0"))
	float VertexToSurfaceBridgeDistance = 0.0f;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGDynMeshToGeometryCollectionElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
