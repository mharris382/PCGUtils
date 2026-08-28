#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Elements/PCGUtilsDynMeshProcessOperation.h"
#include "Selections/GeometrySelection.h"

#include "PCGUtilsDynMeshProcessBase.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;
class UPCGUtilsDynMeshSelectionFactoryData;
class UPCGData;

namespace PCGUtilsDynMeshProcessConstants
{
	inline const FName InputPin = TEXT("In");
	inline const FName SelectionFactoryInputPin = TEXT("Selector");
	inline const FName OutputPin = TEXT("Out");
}

/**
 * Common settings contract for operations that consume either DynMesh data or DynMesh Selection data.
 * The optional Selector is resolved against each input and intersects an incoming materialized selection.
 *
 * A process that has been migrated to the reusable-operation model (see CreateProcessOperation) additionally
 * accepts a DynMesh Builder on its main input and answers with a Builder, deferring the geometry work until a
 * materializer evaluates the chain for a seed. Opt into that with SupportsDeferredBuilderProcessing().
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshProcessBaseSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	/**
	 * Override when ProcessMesh requires selection inputs in one element domain.
	 * Return true and set OutElementType to Vertex, Edge, or Face. The base converts selection-data inputs before
	 * ProcessMesh. A complete Dynamic Mesh input has no selection domain and is never affected by this requirement.
	 * The default returns false, preserving the incoming domain and all existing process behavior.
	 */
	virtual bool GetRequiredSelectionDomain(
		UE::Geometry::EGeometryElementType& OutElementType) const
	{
		return false;
	}

	/**
	 * Override for operations that cannot run without a selection. A materialized Selection input or a connected
	 * Selector satisfies the requirement; a bare DynMesh without a factory produces a graph error.
	 */
	virtual bool RequiresSelection() const { return false; }

	/** Conversion policy used whenever the base implicitly converts an incoming selection domain. */
	virtual bool AllowPartialSelectionDomainInclusion() const { return true; }

	/**
	 * Creates the reusable geometry algorithm this node describes, capturing everything it needs from these
	 * settings *now*. Returning non-null is what makes a process usable in both execution modes: the immediate
	 * executor runs the operation against realized data, and the deferred path stores the very same operation
	 * on a Builder decorator.
	 *
	 * The returned operation must never reach back into a PCG context for its settings - see the warning on
	 * FPCGUtilsDynMeshProcessInvocation::Context.
	 *
	 * The default returns null, meaning "not yet migrated": such a node keeps working in immediate mode through
	 * its own ProcessMesh/ExecuteInternal override and never exposes a Builder pin.
	 */
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const
	{
		return nullptr;
	}

	/**
	 * Migration gate. Only a process whose whole implementation goes through CreateProcessOperation() may
	 * answer true - that is what allows the base to accept a Builder on the main input and emit a Builder
	 * decorator instead of executing. A node whose executor still does work outside its operation must stay
	 * false so no Builder pin is ever exposed for an implementation that could not honour it.
	 */
	virtual bool SupportsDeferredBuilderProcessing() const { return false; }

	/** Evaluation domain for the optional Selector when the derived process does not require a domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProcessSelectionEvaluationDomain SelectionFactoryEvaluationDomain =
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle;

	/**
	 * Output the effective selection tied to the processed mesh so it can be reused downstream.
	 * This controls PCG *pin presentation* in immediate mode only. It never decides whether a Builder chain
	 * keeps an internal active selection - that is the operation's own SelectionOutcome.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	bool bOutputSelectionData = false;

	/** Captures the generic selection behaviour a deferred evaluation cannot rediscover from another context. */
	FPCGUtilsDynMeshProcessSelectionPolicy CaptureSelectionPolicy() const;

	//~Begin UPCGSettings interface
	virtual bool HasDynamicPins() const override { return SupportsDeferredBuilderProcessing(); }
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
	//~End UPCGSettings interface

protected:
	/**
	 * Folds this node's configuration into a deferred Builder decorator's cache identity. The default serializes
	 * every reflected setting, which covers any node whose operation is built purely from its own properties.
	 */
	virtual void AddProcessOperationToCrc(FArchiveCrc32& Ar) const;

	/** The pin carrying the main DynMesh/Selection/Builder input. Derived nodes that relabel it override this. */
	virtual FName GetMainInputPinLabel() const { return PCGUtilsDynMeshProcessConstants::InputPin; }

	/** The pin the result is emitted on. Derived nodes that relabel it override this. */
	virtual FName GetMainOutputPinLabel() const { return PCGUtilsDynMeshProcessConstants::OutputPin; }

	/**
	 * DynMesh | DynMesh Selection, plus DynMesh Builder once this node supports deferred processing. Derived
	 * nodes that declare their own main pin properties must use this rather than a hand-built type set, or
	 * their pin will silently refuse the Builder their executor can actually handle.
	 */
	FPCGDataTypeIdentifier GetProcessDataTypes() const;

	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	friend class FPCGUtilsDynMeshProcessBaseElement;
};

/** Resolved, domain-compatible process input. Any synthesized selection is owned by the PCG context. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshResolvedInput
{
	const UPCGDynamicMeshData* MeshData = nullptr;
	const UPCGDynamicMeshSelectionData* SelectionData = nullptr;

	bool IsValid() const { return MeshData != nullptr; }
	const UPCGData* GetData() const;
};

/**
 * Shared selection/factory resolution used by the process executor, Mesh Target operations, and deferred
 * Builder evaluation.
 *
 * The layering matters: ResolveSelectorFromPin() is the *PCG element* layer and is the only part that inspects
 * a node's input pins. ResolveInput() is the reusable layer - it takes an explicitly supplied Selector and a
 * captured policy, so it works identically whether it is called from the process node's own execution or from
 * a materializer's context long afterwards. Never make the low-level resolver rediscover a Selector by looking
 * at whatever node happens to be executing.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessFunctions
{
public:
	/**
	 * PCG element layer: reads the Selector pin and resolves exactly zero or one Selector factory.
	 * Returns false when the pin content is unusable (already logged); OutFactory may be null on success.
	 */
	static bool ResolveSelectorFromPin(
		FPCGContext* Context, FName PinLabel, const UPCGUtilsDynMeshSelectionFactoryData*& OutFactory);

	/**
	 * Reusable layer: validates the selection requirement, converts selection domains, evaluates the supplied
	 * Selector, intersects it with any incoming materialized selection, and materializes the resolved selection.
	 * Inspects no pins.
	 */
	static FPCGUtilsDynMeshResolvedInput ResolveInput(
		const UPCGData* InputData,
		const UPCGUtilsDynMeshSelectionFactoryData* Selector,
		const FPCGUtilsDynMeshProcessSelectionPolicy& Policy,
		FPCGContext* Context);

	/** Convenience for immediate execution: resolves the Selector from the node's pin, then the input. */
	static FPCGUtilsDynMeshResolvedInput ResolveInput(
		const UPCGData* InputData, const UPCGUtilsDynMeshProcessBaseSettings* Settings, FPCGContext* Context);
};

/** Resolves either input kind, creates an owned mesh, and delegates the mutation to derived processors. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessBaseElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	/**
	 * The pre-operation hook for a process that has *not* been migrated to the reusable-operation model.
	 * A migrated node supplies CreateProcessOperation() instead and needs no executor code at all; the base
	 * runs its operation directly and never calls this.
	 */
	virtual bool ProcessMesh(UPCGDynamicMeshData*, const UPCGDynamicMeshSelectionData*, FPCGContext*) const
	{
		return false;
	}

private:
	/**
	 * Wraps each Builder on the main input in a deferred process decorator instead of touching geometry.
	 * Returns the number of Builder inputs consumed.
	 */
	int32 EmitDeferredBuilders(
		FPCGContext* Context,
		const UPCGUtilsDynMeshProcessBaseSettings* Settings,
		const UPCGUtilsDynMeshSelectionFactoryData* Selector) const;
};
