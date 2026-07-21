#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDeleteDynamicMeshSelection.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDeleteDynamicMeshSelectionSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DeleteDynamicMeshSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDeleteDynamicMeshSelectionElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
