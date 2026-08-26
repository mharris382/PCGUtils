// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGUtilsDynMeshSelectionOperationBase.generated.h"

class UPCGDynamicMeshSelectionData;

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionOperationMode : uint8
{
	Selection UMETA(DisplayName="Selection"),
	Selector UMETA(DisplayName="Selector")
};

namespace PCGUtilsDynMeshSelectionOperationConstants
{
	inline const FName SelectionPin = TEXT("Selection");
}

/**
 * Shared settings base for operations that transform an existing selection.
 * The same node can either process materialized selection data or decorate an upstream selector.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshSelectionOperationSettings
	: public UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsDynMeshSelectionOperationElement;

public:
	/** Materialize immediately, or emit a reusable selector for a downstream DynMesh node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshSelectionOperationMode OperationMode = EPCGUtilsDynMeshSelectionOperationMode::Selection;

	virtual FName GetMainOutputPin() const override;

protected:
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

	/** Pins consumed only while this node is operating as a selector decorator. */
	virtual TArray<FPCGPinProperties> SelectorInputPinProperties() const PURE_VIRTUAL(
		UPCGUtilsDynMeshSelectionOperationSettings::SelectorInputPinProperties, return {};);

	/** Applies the materialized-selection form of the operation. */
	virtual bool ProcessSelection(
		const UPCGDynamicMeshSelectionData* SelectionData,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const PURE_VIRTUAL(
		UPCGUtilsDynMeshSelectionOperationSettings::ProcessSelection, return false;);
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionOperationElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
