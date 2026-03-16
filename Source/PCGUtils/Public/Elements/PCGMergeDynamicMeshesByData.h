#pragma once

#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGMergeDynamicMeshesByData.generated.h"

/**
 * Groups incoming dynamic meshes by a shared @Data FSoftObjectPath attribute value,
 * merges each group into a single mesh, and outputs one merged mesh per unique value.
 * Meshes missing the attribute or with a null value are merged into a single null-key group.
 * 
 * This minimizes downstream loop iterations — N meshes sharing the same override graph
 * become 1 merged mesh, requiring only 1 loop iteration instead of N.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGMergeDynamicMeshesByDataSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	// The @Data attribute name to group by. Expected to be an FSoftObjectPath
	// (e.g. an override graph reference stamped by ResolveOverrideGraphs).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName GroupByAttributeName = NAME_None;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGMergeDynamicMeshesByDataElement : public IPCGDynamicMeshBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};