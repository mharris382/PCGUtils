// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionOperationBase.h"

#include "PCGDynMeshExpandContractSelection.generated.h"

namespace PCGDynMeshExpandContractSelectionConstants
{
	inline const FName SelectionPin = TEXT("Selection");
	inline const FName SeedSelectorPin = TEXT("Seed Selector");
}

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshExpandContractSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> SeedFactory;

	UPROPERTY()
	int32 Iterations = 1;

	UPROPERTY()
	bool bContract = false;

	UPROPERTY()
	bool bOnlyExpandToFaceNeighbours = false;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Grows or shrinks an existing DynMesh selection across connected mesh elements. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshExpandContractSelectionSettings : public UPCGUtilsDynMeshSelectionOperationSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExpandContractSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	/** Number of connected-neighbour growth or shrink steps. Zero passes the selection through unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0", ClampMax="100", UIMin="0", UIMax="10"))
	int32 Iterations = 1;

	/** Shrink the selection instead of growing it. Set by the Expand/Contract palette preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bContract = false;

	/** For triangle selections, grow only across shared edges instead of any shared vertex. Ignored when contracting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, EditCondition="!bContract", EditConditionHides))
	bool bOnlyExpandToFaceNeighbours = false;

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
