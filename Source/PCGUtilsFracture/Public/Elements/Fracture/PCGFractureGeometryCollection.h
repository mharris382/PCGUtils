// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGFractureGeometryCollection.generated.h"

namespace PCGFractureGeometryCollectionConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName CollectionOutputPin = TEXT("GC");
}

/**
 * The one executor that runs any fracture operation.
 *
 * It contains no Voronoi-, plane-, slice- or cutter-specific behaviour: it validates and copies the
 * collection, resolves the target bones, and hands both to whatever Fracture operations are connected. New
 * fracture types are added as operations, never by editing this node.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection Apply Fracture Shatter Break GC"))
class PCGUTILSFRACTURE_API UPCGFractureGeometryCollectionSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("FractureGC"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Reassign the material slot of every interior (fracture-generated) face after the operations run.
	 *
	 * Lives here rather than on each Fracture operation because it is a property of the result, not of any one
	 * fracture algorithm - Unreal's fracture entry points do not expose an internal material id at all.
	 * Note it retags every internal face, including ones produced by an earlier fracture in the same chain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials", meta=(PCG_Overridable))
	bool bOverrideInternalMaterial = false;

	/** Index into the collection's material array. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials",
		meta=(PCG_Overridable, EditCondition="bOverrideInternalMaterial", ClampMin="0"))
	int32 InternalMaterialID = 0;

	/**
	 * Keep the un-fractured shape of every bone that was cut, hidden inside the collection.
	 *
	 * Unreal's cutters do not delete the geometry they replace - they mark every one of its faces invisible
	 * and leave it on the bone, which is now a cluster. Nothing downstream can use it: the engine's own
	 * converter, Fracture Mode and every node in this module read pieces (rigid bones with geometry) and skip
	 * clusters. It is simply carried along, and a second fracture level stacks another hidden copy on top.
	 *
	 * Leave this off. Turn it on only if you specifically need the pre-fracture surface still present in the
	 * collection, and expect the vertex and face counts to grow with every fracture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", AdvancedDisplay, meta=(PCG_Overridable))
	bool bKeepHiddenSourceGeometry = false;

	/** Emit a reusable Selector containing every bone created by this fracture execution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	bool bOutputResultSelector = false;

	virtual bool HasDynamicPins() const override { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGFractureGeometryCollectionElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
