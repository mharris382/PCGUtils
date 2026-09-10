// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGUtilsFractureProvider.generated.h"

class FGeometryCollection;
class UPCGUtilsFractureFactoryData;

namespace PCGUtilsFractureProviderConstants
{
	/** Second output pin: a reusable Selection of the bones the operation created. */
	inline const FName ResultOutputPin = TEXT("Result");

	/** Transform-group attribute prefix for a result tag whose name was generated rather than given. */
	inline const FName ResultTagAttributePrefix = TEXT("GC_Result_");
}

/**
 * Selects the bones one fracture operation created, by reading the tag that operation stamped on them.
 *
 * Deliberately a *deferred* selector rather than a materialized selection, for the same reason Extrude emits a
 * reusable Result Selector instead of Selection data: the bones do not exist yet when the authoring node runs.
 * The shared key is the tag attribute name - generated from the authoring node's path, or given explicitly -
 * which is exactly how the DynMesh Result Selector uses a named PolyGroup layer.
 *
 * Bone indices are never stored. A tag is a Transform-group attribute, so it travels with its bone through the
 * reindexing that fracture and prune both do, and the selection stays correct across later revisions in a way a
 * recorded index set never could.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsFractureResultSelectionFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	/** Transform-group attribute carrying 1 on the bones the operation created. */
	UPROPERTY()
	FName ResultTagAttribute;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Shared provider base for every fracture authoring node.
 *
 * Two things live here rather than being repeated per operation:
 *
 *   - **Priority.** A Fracture pin is multi-connection and the executor runs the operations in priority order,
 *     so an operation that cannot state its priority cannot be sequenced. Every other factory family exposes
 *     it; fracture nodes were the one family that did not, which made ordering a matter of connection order.
 *   - **The Result selection.** A second output pin carrying a Selection of the bones this operation created,
 *     so a graph can fracture and then keep working on the fragments - paint them, prune a subset, fracture
 *     them again - without re-deriving which bones are new from point attributes.
 *
 * The Result pin is always declared, so a PCG property override can enable the output without changing graph
 * wiring, and it emits data only when the flag is on.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGUtilsFractureProviderSettings
	: public UPCGUtilsGeometryCollectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** Execution order when several operations share one Fracture pin. Lower runs first; ties keep wiring order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	/**
	 * Emit a reusable Selection of the bones this operation creates, on the Result pin.
	 *
	 * This is what makes "fracture, then process the resulting pieces" a two-node graph. The selection is
	 * resolved when it is used, not when it is authored, so it stays correct through later operations.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	bool bOutputResultSelection = false;

	/**
	 * Transform-group attribute the operation stamps on the bones it creates, and the Result selection reads
	 * back. None generates a name from this node's path, which is stable across property overrides and
	 * saved-graph reloads; a copied or renamed node gets its own.
	 *
	 * Name it explicitly when something other than this node's Result pin needs to find the same bones - the
	 * attribute is ordinary collection data, so a second Result selection pointed at the same name selects the
	 * same set. Reusing one name across two operations merges their results rather than keeping them apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result",
		meta=(PCG_Overridable, EditCondition="bOutputResultSelection"))
	FName ResultTagAttribute = NAME_None;

	/** The effective attribute name, resolving None to one generated from this node's path. */
	FName GetResultTagAttribute() const;

	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual FName GetMainOutputPin() const override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/**
	 * Builds the operation this node describes. Derived nodes override this instead of CreateFactory, which
	 * owns the shared Priority and result-tag plumbing and must not be bypassed.
	 */
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
		PURE_VIRTUAL(UPCGUtilsFractureProviderSettings::CreateFractureFactory, return nullptr;);

	virtual FPCGElementPtr CreateElement() const override;

	friend class FPCGUtilsFractureProviderElement;
};

/** Emits the Fracture operation, plus the Result selection when it is enabled. */
class PCGUTILSFRACTURE_API FPCGUtilsFractureProviderElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};

namespace PCGUtilsFractureResultTagging
{
	/**
	 * Stamps the result tag on every bone created since the last call to PrepareForOperation.
	 *
	 * Identifying "new" generically, without each operation having to cooperate, rests on one fact: bone ids
	 * are minted by the revision publisher, *after* fracture. So immediately before an operation runs we mint
	 * ids for everything that exists (PrepareForOperation), and afterwards any bone still without one was
	 * created by that operation. That holds even though the cutters reindex, which an index-range comparison
	 * would not survive.
	 *
	 * Minting early is safe: EnsureBoneIds never reassigns an existing id, so the publisher's own call later
	 * is a no-op for these bones.
	 */
	PCGUTILSFRACTURE_API void PrepareForOperation(FGeometryCollection& InOutCollection);

	/**
	 * @param InResultTagAttribute  No-op when None - an operation whose node did not ask for a Result pin
	 *                              should not pay for tagging or add an attribute to the collection.
	 * @return number of bones tagged.
	 */
	PCGUTILSFRACTURE_API int32 TagBonesCreatedByOperation(
		FGeometryCollection& InOutCollection, FName InResultTagAttribute);
}
