// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Elements/Selections/PCGSelectionBoundaryEdges.h"

#include "PCGDynMeshSelectionBoundaryFactory.generated.h"

namespace PCGDynMeshSelectionBoundaryFactoryConstants
{
	inline const FName RegionFactoryInputPin = TEXT("Region Selector");
}

/** Edge-output factory that computes the boundary of the region selected by one child selector. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionBoundaryFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> RegionFactory;

	UPROPERTY()
	bool bExcludeMeshBoundaryEdges = false;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Edge;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(DeprecatedNode, DeprecationMessage="Use Select Boundary with Operation Mode set to Selector."))
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionBoundaryFactoryProviderSettings
	: public UPCGSelectionBoundaryEdgesSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshSelectionBoundaryFactoryProviderSettings()
	{
		OperationMode = EPCGUtilsDynMeshSelectionOperationMode::Selector;
	}
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionBoundaryFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

};
