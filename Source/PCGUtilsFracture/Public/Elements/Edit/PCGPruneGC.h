// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGPruneGC.generated.h"

namespace PCGPruneGCConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName CollectionOutputPin = TEXT("GC");
}

/**
 * Deletes the selected bones, and everything under them, from the Geometry Collection.
 *
 * This genuinely removes the geometry from the intermediate collection - it is not a visibility trick or a
 * filter applied during the final conversion - which is what makes "fracture, then delete some pieces" a real
 * modelling operation rather than a rendering one.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection Prune Delete Remove Bones Fracture GC"))
class PCGUTILSFRACTURE_API UPCGPruneGCSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("PruneGC"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Keep the selected bones and delete everything else instead. Applied to the resolved selection before
	 * pruning, so all the same root/hierarchy rules still hold.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prune", meta=(PCG_Overridable))
	bool bInvertSelection = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGPruneGCElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
