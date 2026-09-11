// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionTypes.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Selections/GeometrySelection.h"

#include "PCGUtilsDynMeshSelectionSource.generated.h"

namespace PCGUtilsDynMeshSelectionSourceConstants
{
	inline const FName CandidatesPin = TEXT("Candidates");
	inline const FName SelectionPin = TEXT("Selection");
	inline constexpr int32 SelectorPreconfiguredIndex = 1000;
	inline constexpr int32 SelectionPreconfiguredIndex = 1001;
}

/**
 * Shared public contract for selection queries. One element authors the same predicate as either a deferred
 * Selector or an immediately materialized, mesh-bound Selection.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshSelectionSourceSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsDynMeshSelectionSourceElement;

public:
	/** Complement the selector in whichever vertex, edge, or face domain consumes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bInvertSelection = false;

	/** Deferred selectors compose without a mesh; materialized selections evaluate immediately against Candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, DisplayName="Representation"))
	EPCGUtilsDynMeshSelectionRepresentation Representation =
		EPCGUtilsDynMeshSelectionRepresentation::Selector;

	/** Derived queries can lock materialization to an intrinsic algorithm domain. */
	UPROPERTY(Transient)
	bool bSupportsMaterializedElementTypeOverride = true;

	/** Vertex, edge, or triangle domain used only when materializing this selector inline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, DisplayName="Element Type",
			EditCondition="Representation==EPCGUtilsDynMeshSelectionRepresentation::Selection && bSupportsMaterializedElementTypeOverride",
			EditConditionHides))
	EPCGUtilsDynMeshSelectionElementType SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Triangle;

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
	/** Centralized palette formatting for one canonical query name in both public representations. */
	static TArray<FPCGPreConfiguredSettingsInfo> MakeRepresentationPresets(
		const FText& DisplayName, int32 SelectorIndex, int32 SelectionIndex);
	static bool ApplyRepresentationPreset(int32 PreconfiguredIndex, int32 SelectorIndex,
		int32 SelectionIndex, EPCGUtilsDynMeshSelectionRepresentation& OutRepresentation);
#endif

	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const final;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

	/** Non-mesh operands used to configure this selection query, such as points, targets, or child selectors. */
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const { return {}; }

	/** Domain requested when the Selection representation is materialized. */
	virtual UE::Geometry::EGeometryElementType GetMaterializedElementType() const;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionSourceElement final : public IPCGElement
{
public:
	/** Selector materialization may initialize spatial queries that resolve PCG actor transforms. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
