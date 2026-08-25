#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCGUtilsGraphBatch.generated.h"

class IPCGGraphExecutionSource;
class UPCGDefaultExecutionSource;
class UPCGGraphInterface;
enum class EPCGGenerationStatus : uint8;

/** A single standalone asset graph invocation in a PCG graph batch. */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsGraphBatchEntry
{
	GENERATED_BODY()

	/** Disabled entries are ignored when the batch is run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
	bool bEnabled = true;

	/**
	 * Standalone PCG asset graph to execute. This may be either a PCG Graph or a
	 * PCG Graph Instance containing a saved set of graph parameter overrides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
	TObjectPtr<UPCGGraphInterface> Graph = nullptr;

	/** Seed supplied to this graph execution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
	int32 Seed = 42;
};

/**
 * Editor-only, reusable list of standalone PCG asset graphs.
 *
 * Entries execute sequentially so asset-producing graphs cannot race each
 * other. Use PCG Graph Instance assets when an entry needs saved parameters.
 */
UCLASS(BlueprintType, meta = (DisplayName = "PCG Graph Batch"))
class PCGUTILS_API UPCGUtilsGraphBatch : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Graphs execute in array order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Batch")
	TArray<FPCGUtilsGraphBatchEntry> Graphs;

	/** Do not execute later entries after a graph aborts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Batch")
	bool bStopOnFailure = true;

	/** Snapshot of the entries participating in an in-flight editor execution. */
	UPROPERTY(Transient)
	TArray<FPCGUtilsGraphBatchEntry> ExecutionQueue;

	virtual bool IsEditorOnly() const override { return true; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

	/** Starts a validated, sequential execution of all enabled entries. */
	void RunBatch();

	/** Requests cancellation of the currently executing graph. */
	void CancelBatch();

	bool IsBatchRunning() const { return bBatchRunning; }
	bool CanRunBatch() const;
	FText GetExecutionStatusText() const { return ExecutionStatus; }

private:
	bool ValidateEntry(const FPCGUtilsGraphBatchEntry& Entry, int32 EntryIndex, FText& OutError) const;
	void ExecuteNextGraph();
	void OnGraphGenerationFinished(IPCGGraphExecutionSource* Source, EPCGGenerationStatus Status);
	void FinishBatch(const FText& FinalStatus);

	bool bBatchRunning = false;
	bool bCancelRequested = false;
	bool bStopOnFailureForRun = true;
	int32 NextExecutionIndex = 0;
	int32 CompletedGraphCount = 0;
	FText ExecutionStatus;
	TWeakObjectPtr<UPCGDefaultExecutionSource> ActiveExecutionSource;

#endif // WITH_EDITOR
};
