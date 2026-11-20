// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGUtilsDynMeshRoutedSelectionFactory.generated.h"

/** One mesh-specific branch of a bundled result selector. */
USTRUCT()
struct FPCGUtilsDynMeshSelectionRoute
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<const UPCGDynamicMeshData> Mesh;
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> Selector;
};

/** Internal switchboard: one selector datum for a category across any number of result meshes. */
UCLASS()
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshRoutedSelectionFactoryData : public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FPCGUtilsDynMeshSelectionRoute> Routes;
	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;
protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

namespace PCGUtilsDynMeshSelectionRouting
{
	/** Native PCG metadata parenting preserves identity through InitializeFromData and DuplicateData.
	 * Siblings, including geometrically identical outputs of separate operations, never match each other.
	 * INDEX_NONE means unrelated. A smaller distance identifies the most specific matching result.
	 */
	PCGUTILSDYNMESH_API int32 GetMatchDistance(const UPCGDynamicMeshData* Target, const UPCGDynamicMeshData* Reference);
}
