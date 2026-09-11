// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowSelection.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionFactoryData.h"

#include "PCGUtilsGeometryCollectionSelectionFactory.generated.h"

class FGeometryCollection;
class UPCGGeometryCollectionData;

namespace PCGUtilsGeometryCollectionSelectionFactoryConstants
{
	inline const FName OutputPin = TEXT("Selection");
	inline const FName SelectionInputPin = TEXT("Selection");
}

USTRUCT(meta=(PCG_DataTypeDisplayName="GC Selection"))
struct FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo : public FPCGUtilsGeometryCollectionFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSFRACTURE_API);
};

/** Read-only collection state shared by every factory in one selection evaluation. */
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionSelectionEvaluationContext
{
	FPCGUtilsGeometryCollectionSelectionEvaluationContext(
		const UPCGGeometryCollectionData& InCollectionData,
		const FGeometryCollection& InCollection)
		: CollectionData(InCollectionData), Collection(InCollection)
	{
	}

	/**
	 * The published data the selection is being evaluated against.
	 *
	 * Always present. A selection is only meaningful relative to an identified collection state - that is what
	 * lets Select Bones From Points reject stale indices - and a derived-data cache can only be keyed on a
	 * data object, so evaluating against an anonymous working copy is not something a caller may ask for.
	 * An executor mutating a collection mid-chain must publish an intermediate revision instead.
	 */
	const UPCGGeometryCollectionData& CollectionData;

	/**
	 * The live collection. Normally the same state CollectionData holds; an executor may pass its private
	 * working copy so long as it is still structurally identical to the published state.
	 */
	const FGeometryCollection& Collection;

	int32 NumTransforms() const;
};

/**
 * Describes WHICH Geometry Collection bones an operation should affect.
 *
 * Deliberately set-valued rather than a per-bone predicate. The DynMesh selection factories test one element at
 * a time, which suits vertex/edge/face predicates, but the useful bone selectors are inherently set-valued:
 * FCollectionTransformSelectionFacade::SelectContact, SelectLevel, SelectSiblings and SelectByPercentage all
 * compute over the whole hierarchy at once and cannot be expressed as TestElement(BoneIndex). Forcing them into
 * a predicate shape would mean re-deriving the whole set on every call.
 *
 * Implementations must return a selection already sized to the collection's transform count - use
 * InitializeFromCollection. Every FractureEngine entry point rejects a mis-sized selection, which is a useful
 * free structural check on top of our own identity/revision validation.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionSelectionFactoryData : public UPCGUtilsGeometryCollectionFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo)

	/** Complement this selector across every transform in the evaluated collection. */
	UPROPERTY()
	bool bInvertSelection = false;

	/** Returns false (already logged) if the selection could not be resolved against this collection state. */
	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const
		PURE_VIRTUAL(UPCGUtilsGeometryCollectionSelectionFactoryData::Evaluate, return false;);

	/** Applies this selector's logical complement in its meaningful candidate domain. */
	virtual void ApplyInversion(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FDataflowTransformSelection& InOutSelection) const;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Shared settings base for every node that authors a reusable GC Selector. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
	: public UPCGUtilsGeometryCollectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** Select the complement of this selector, avoiding a separate NOT node for the common inside/outside choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bInvertSelection = false;

	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;
};

namespace PCGUtilsGeometryCollectionSelectionFactories
{
	/** The set of PCG data types accepted anywhere a GC Selection is expected. */
	PCGUTILSFRACTURE_API const TSet<FPCGDataTypeBaseId>& GetSelectionFactoryTypes();

	/**
	 * Evaluates every factory and unions the results into one selection sized to the collection.
	 *
	 * Several selectors reaching one place mean "all of these bones", both on a multi-connection pin and
	 * inside a decorator holding captured children - so both go through here and cannot drift apart. Use the
	 * Selection Logic node for any other combination.
	 *
	 * @return false if any factory failed (it will have logged) or resolved against a different collection.
	 */
	PCGUTILSFRACTURE_API bool EvaluateAndUnion(
		TConstArrayView<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> InFactories,
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection);

	/**
	 * Evaluates every factory connected to InPinLabel and unions the results.
	 *
	 * @param bRequired  When false, an unconnected pin succeeds with bOutHasSelection = false, which callers
	 *                   treat as "no selection authored" rather than as an error.
	 */
	PCGUTILSFRACTURE_API bool ResolveSelectionFromPin(
		FPCGContext* InContext,
		FName InPinLabel,
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		bool bRequired,
		FDataflowTransformSelection& OutSelection,
		bool& bOutHasSelection);
}
