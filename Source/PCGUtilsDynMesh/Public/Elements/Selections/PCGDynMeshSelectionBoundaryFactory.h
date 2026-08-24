// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshSelectionBoundaryFactory.generated.h"

namespace PCGDynMeshSelectionBoundaryFactoryConstants
{
	inline const FName RegionFactoryInputPin = TEXT("Region Factory");
}

/** Domain in which the child factory is evaluated before its result is converted to a triangle region. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshBoundarySourceElementType : uint8
{
	Triangle,
	Vertex,
	Edge
};

/** Edge-output factory that computes the boundary of the region selected by one child factory. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionBoundaryFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> RegionFactory;

	UPROPERTY()
	EPCGUtilsDynMeshBoundarySourceElementType SourceElementType =
		EPCGUtilsDynMeshBoundarySourceElementType::Triangle;

	UPROPERTY()
	bool bExcludeMeshBoundaryEdges = false;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionBoundaryFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionBoundaryFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Domain used to evaluate the child region factory. The final Build node must use the Edge domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshBoundarySourceElementType SourceElementType =
		EPCGUtilsDynMeshBoundarySourceElementType::Triangle;

	/** Do not include region-boundary edges that are also open boundaries of the mesh itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bExcludeMeshBoundaryEdges = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
