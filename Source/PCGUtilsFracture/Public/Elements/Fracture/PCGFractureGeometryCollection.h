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
