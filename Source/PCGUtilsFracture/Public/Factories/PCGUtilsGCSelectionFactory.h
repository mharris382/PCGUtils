// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowSelection.h"
#include "Factories/PCGUtilsGCFactoryData.h"

#include "PCGUtilsGCSelectionFactory.generated.h"

class FGeometryCollection;
class UPCGGeometryCollectionData;

namespace PCGUtilsGCSelectionFactoryConstants
{
	inline const FName OutputPin = TEXT("Selection");
	inline const FName SelectionInputPin = TEXT("Selection");
}

USTRUCT(meta=(PCG_DataTypeDisplayName="GC Selection"))
struct FPCGUtilsGCSelectionFactoryDataTypeInfo : public FPCGUtilsGCFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSFRACTURE_API);
};

/** Read-only collection state shared by every factory in one selection evaluation. */
struct PCGUTILSFRACTURE_API FPCGUtilsGCSelectionEvaluationContext
{
	FPCGUtilsGCSelectionEvaluationContext(
		const UPCGGeometryCollectionData* InCollectionData,
		const FGeometryCollection& InCollection)
		: CollectionData(InCollectionData), Collection(InCollection)
	{
	}

	/**
	 * The data the selection is being evaluated against. May be null when a factory is evaluated against a
	 * working copy mid-operation; identity-checking factories must handle that by failing loudly.
	 */
	const UPCGGeometryCollectionData* CollectionData = nullptr;

	/** The live collection - post-copy, and possibly already mutated by an earlier factory in the same node. */
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
class PCGUTILSFRACTURE_API UPCGUtilsGCSelectionFactoryData : public UPCGUtilsGCFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsGCSelectionFactoryDataTypeInfo)

	/** Returns false (already logged) if the selection could not be resolved against this collection state. */
	virtual bool Evaluate(
		const FPCGUtilsGCSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const
		PURE_VIRTUAL(UPCGUtilsGCSelectionFactoryData::Evaluate, return false;);
};

namespace PCGUtilsGCSelectionFactories
{
	/** The set of PCG data types accepted anywhere a GC Selection is expected. */
	PCGUTILSFRACTURE_API const TSet<FPCGDataTypeBaseId>& GetSelectionFactoryTypes();

	/**
	 * Evaluates every factory connected to InPinLabel and unions the results.
	 *
	 * @param bRequired  When false, an unconnected pin succeeds with bOutHasSelection = false, which callers
	 *                   treat as "no selection authored" rather than as an error.
	 */
	PCGUTILSFRACTURE_API bool ResolveSelectionFromPin(
		FPCGContext* InContext,
		FName InPinLabel,
		const FPCGUtilsGCSelectionEvaluationContext& InEvaluationContext,
		bool bRequired,
		FDataflowTransformSelection& OutSelection,
		bool& bOutHasSelection);
}
