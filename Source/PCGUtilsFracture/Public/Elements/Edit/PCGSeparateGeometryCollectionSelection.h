// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGSeparateGeometryCollectionSelection.generated.h"

namespace PCGSeparateGeometryCollectionSelectionConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName SelectionInputPin = TEXT("Selection");
	inline const FName SelectedOutputPin = TEXT("Selected");
	inline const FName UnselectedOutputPin = TEXT("Unselected");
}

/** Splits one GC into selected and unselected piece sets while preserving the hierarchy each half needs. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection GC Separate Split Selection Selector Bones Pieces"))
class PCGUTILSFRACTURE_API UPCGSeparateGeometryCollectionSelectionSettings
	: public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SeparateGCSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGSeparateGeometryCollectionSelectionElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
