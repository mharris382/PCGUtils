#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDynamicMeshSelectionToPoints.generated.h"

/** Converts the unique vertices belonging to selected mesh faces into PCG points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionToPointsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynamicMeshSelectionToPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Transform mesh-local vertex positions and normals into the PCG target actor's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionToPointsElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
