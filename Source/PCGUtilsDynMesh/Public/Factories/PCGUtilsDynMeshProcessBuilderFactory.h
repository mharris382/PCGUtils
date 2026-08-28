// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessOperation.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"

#include "PCGUtilsDynMeshProcessBuilderFactory.generated.h"

class UPCGUtilsDynMeshSelectionFactoryData;

/**
 * The generic unary Builder decorator: one child Builder expression, one reusable process operation, an
 * optional Selector, and the captured generic selection policy of the process node that authored it.
 *
 * Emitted by FPCGUtilsDynMeshProcessBaseElement when a migrated process receives a Builder on its main input.
 * The process node itself does no geometry work at that point - it only captures its own configuration.
 *
 * ARCHITECTURE NOTES for future maintainers:
 *
 * - `Operation` is a native immutable object rather than a UObject. That is safe here because the operation
 *   holds only value types and soft references: it captures no live `FPCGContext*` (which would be stale by
 *   evaluation time) and no hard `UObject*` (which would be invisible to GC through a non-UPROPERTY member).
 *   Factory data declares `CanBeSerialized() == false`, so nothing ever tries to persist it either. An
 *   operation that genuinely needs to hold engine objects must instead park them on a UPROPERTY here, or on a
 *   UObject config snapshot referenced by one.
 * - Evaluation must not read the *materializer's* settings. Everything the decorator needs - Policy,
 *   Operation, Selector - was captured while the authoring node executed.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Builders")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshProcessBuilderFactoryData : public UPCGUtilsDynMeshBuilderFactoryData
{
	GENERATED_BODY()

public:
	/** The Builder expression this decorator wraps. */
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData> ChildBuilder;

	/** Optional Selector captured from the authoring node's Selector pin. */
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> Selector;

	/** The reusable geometry algorithm, fully configured by the authoring node. */
	TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation;

	/** The authoring node's generic selection behaviour, captured so it is never rediscovered from a context. */
	FPCGUtilsDynMeshProcessSelectionPolicy Policy;

	/** CRC of the authoring node's configuration, folded into this decorator's cache identity. */
	UPROPERTY()
	uint32 OperationConfigCrc = 0;

	/** Node title of the authoring process, used only to make deferred-evaluation errors identifiable. */
	UPROPERTY()
	FName ProcessLabel;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};
