// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionOperationBase.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "GeometryScript/MeshSelectionFunctions.h"

#include "PCGDynMeshExpandToConnectedSelection.generated.h"

namespace PCGDynMeshExpandToConnectedSelectionConstants
{
	inline const FName SelectionPin = TEXT("Selection");
	inline const FName SeedFactoryInputPin = TEXT("Seed Selector");
}

/** Expands a materialized triangle selection to complete connected regions. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshExpandToConnectedSelectionSettings : public UPCGUtilsDynMeshSelectionOperationSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExpandToConnectedSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Constraint that triangles must satisfy while traversing away from the seed selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EGeometryScriptTopologyConnectionType ConnectionType = EGeometryScriptTopologyConnectionType::Geometric;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual TArray<FPCGPinProperties> SelectorInputPinProperties() const override;
	virtual bool ProcessSelection(
		const UPCGDynamicMeshSelectionData* SelectionData,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};

/** Factory that caches the connected triangle regions reached from one child seed selector. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshExpandToConnectedSelectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> SeedFactory;

	UPROPERTY()
	EGeometryScriptTopologyConnectionType ConnectionType = EGeometryScriptTopologyConnectionType::Geometric;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(DeprecatedNode, DeprecationMessage="Use Select Connected with Operation Mode set to Selector."))
class PCGUTILSDYNMESH_API UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings
	: public UPCGDynMeshExpandToConnectedSelectionSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings()
	{
		OperationMode = EPCGUtilsDynMeshSelectionOperationMode::Selector;
	}
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExpandToConnectedSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

};
