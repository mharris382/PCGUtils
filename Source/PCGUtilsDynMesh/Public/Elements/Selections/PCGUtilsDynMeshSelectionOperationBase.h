// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGUtilsDynMeshSelectionOperationBase.generated.h"

class UPCGDynamicMeshSelectionData;

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionOperationMode : uint8
{
	Selection UMETA(DisplayName="Selection (Materialized)"),
	Selector UMETA(DisplayName="Selector (Deferred)")
};

namespace PCGUtilsDynMeshSelectionOperationConstants
{
	inline const FName SelectionPin = TEXT("Selection");
	inline constexpr int32 SelectorPreconfiguredIndex = 1000;
	inline constexpr int32 SelectionPreconfiguredIndex = 1001;
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
	/** Choose whether this element emits a reusable deferred Selector or materializes a mesh-bound Selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, DisplayName="Representation"))
	EPCGUtilsDynMeshSelectionOperationMode OperationMode = EPCGUtilsDynMeshSelectionOperationMode::Selection;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

#if WITH_EDITOR
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

protected:
#if WITH_EDITOR
	/** Centralized palette formatting for one canonical operation name in both public representations. */
	static TArray<FPCGPreConfiguredSettingsInfo> MakeRepresentationPresets(
		const FText& DisplayName, int32 SelectorIndex, int32 SelectionIndex);
	static bool ApplyRepresentationPreset(int32 PreconfiguredIndex, int32 SelectorIndex,
		int32 SelectionIndex, EPCGUtilsDynMeshSelectionOperationMode& OutMode);
#endif

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

	/** Creates the one canonical decorator implementation around either a deferred or materialized child. */
	virtual UPCGUtilsDynMeshSelectionFactoryData* CreateDecoratorFactory(
		FPCGContext* InContext,
		const UPCGUtilsDynMeshSelectionFactoryData* ChildSelector) const PURE_VIRTUAL(
		UPCGUtilsDynMeshSelectionOperationSettings::CreateDecoratorFactory, return nullptr;);

	/** Output domain used when materializing this decorator. Most modifiers preserve their input domain. */
	virtual UE::Geometry::EGeometryElementType GetMaterializedOutputElementType(
		const UPCGDynamicMeshSelectionData* SelectionData) const;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionOperationElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
