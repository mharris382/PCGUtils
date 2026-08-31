// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGGeometryCollectionToDynMesh.generated.h"

namespace PCGGeometryCollectionToDynMeshConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName MeshOutputPin = TEXT("DynMesh");

	/**
	 * Default name of the per-bone PolyGroup layer written on the output mesh. Read it back with Select by
	 * PolyGroup's Group Layer Name field to isolate individual fracture pieces once you are back in DynMesh.
	 */
	inline const FName DefaultBonePolygroupLayer = TEXT("GC_Bone");
}

/**
 * Combines the surviving Geometry Collection pieces back into a single DynMesh, preserving fracture-piece
 * identity and interior/exterior surface tagging as named PolyGroup layers.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection To Mesh Fracture GC"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionToDynMeshSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCToDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Write each source bone's index into a named PolyGroup layer, so fracture pieces stay individually
	 * selectable in the DynMesh ecosystem. Values are indices into the *incoming* collection state - after a
	 * Prune, bones are reindexed, so these are post-prune indices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups", meta=(PCG_Overridable))
	bool bSetPolygroupPerBone = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups",
		meta=(PCG_Overridable, EditCondition="bSetPolygroupPerBone"))
	FName BonePolygroupLayerName = PCGGeometryCollectionToDynMeshConstants::DefaultBonePolygroupLayer;

	/**
	 * Keep the engine's interior/exterior face tagging as the named PolyGroup layer
	 * "GeometryCollectionInternalFaces". This is what makes "select only the walls of the cavity I carved"
	 * work through the existing Select by PolyGroup node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups", meta=(PCG_Overridable))
	bool bTagInternalFaces = true;

	/** Weld edges at normal/UV/colour seams so they behave as one edge during later processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion", meta=(PCG_Overridable))
	bool bWeldVertices = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion", AdvancedDisplay, meta=(PCG_Overridable))
	bool bPreserveIsolatedVertices = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionToDynMeshElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
