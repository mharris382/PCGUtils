#include "Data/PCGUtilsGraphBatch.h"

#if WITH_EDITOR
#include "PCGDefaultExecutionSource.h"
#include "PCGGraph.h"
#include "PCGModule.h"
#include "Subsystems/IPCGBaseSubsystem.h"
#include "Subsystems/PCGEngineSubsystem.h"

#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "PCGUtilsGraphBatch"

#if WITH_EDITOR

namespace PCGUtilsGraphBatch
{
	FText GetEntryName(const FPCGUtilsGraphBatchEntry& Entry, const int32 EntryIndex)
	{
		return Entry.Graph
			? Entry.Graph->GetDisplayName()
			: FText::Format(LOCTEXT("EntryFallbackName", "Entry {0}"), FText::AsNumber(EntryIndex + 1));
	}
}

EDataValidationResult UPCGUtilsGraphBatch::IsDataValid(FDataValidationContext& Context) const
{
	bool bHasError = false;
	bool bHasEnabledEntry = false;
	TSet<const UPCGGraphInterface*> SeenGraphs;

	for (int32 EntryIndex = 0; EntryIndex < Graphs.Num(); ++EntryIndex)
	{
		const FPCGUtilsGraphBatchEntry& Entry = Graphs[EntryIndex];
		if (!Entry.bEnabled)
		{
			continue;
		}

		bHasEnabledEntry = true;

		FText Error;
		if (!ValidateEntry(Entry, EntryIndex, Error))
		{
			Context.AddError(Error);
			bHasError = true;
			continue;
		}

		if (SeenGraphs.Contains(Entry.Graph))
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("DuplicateGraphWarning", "Entry {0} executes '{1}' more than once."),
				FText::AsNumber(EntryIndex + 1),
				Entry.Graph->GetDisplayName()));
		}
		SeenGraphs.Add(Entry.Graph);
	}

	if (!bHasEnabledEntry)
	{
		Context.AddError(LOCTEXT("NoEnabledGraphs", "The batch has no enabled graph entries."));
		bHasError = true;
	}

	return bHasError ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

bool UPCGUtilsGraphBatch::ValidateEntry(
	const FPCGUtilsGraphBatchEntry& Entry,
	const int32 EntryIndex,
	FText& OutError) const
{
	if (!Entry.Graph)
	{
		OutError = FText::Format(
			LOCTEXT("NullGraphError", "Entry {0} has no graph."),
			FText::AsNumber(EntryIndex + 1));
		return false;
	}

	if (!Entry.Graph->GetGraph())
	{
		OutError = FText::Format(
			LOCTEXT("MissingUnderlyingGraphError", "Entry {0} ('{1}') has no underlying PCG graph."),
			FText::AsNumber(EntryIndex + 1),
			Entry.Graph->GetDisplayName());
		return false;
	}

	if (Entry.Graph->GetGraphUsage() != EPCGGraphUsage::Asset)
	{
		OutError = FText::Format(
			LOCTEXT("WrongGraphUsageError", "Entry {0} ('{1}') must use the Asset graph usage context."),
			FText::AsNumber(EntryIndex + 1),
			Entry.Graph->GetDisplayName());
		return false;
	}

	return true;
}

bool UPCGUtilsGraphBatch::CanRunBatch() const
{
	return !bBatchRunning && Graphs.ContainsByPredicate([](const FPCGUtilsGraphBatchEntry& Entry)
	{
		return Entry.bEnabled && Entry.Graph != nullptr;
	});
}

void UPCGUtilsGraphBatch::RunBatch()
{
	if (bBatchRunning)
	{
		return;
	}

	if (!UPCGEngineSubsystem::Get())
	{
		ExecutionStatus = LOCTEXT("SubsystemUnavailable", "PCG engine subsystem is unavailable.");
		UE_LOGF(LogPCG, Error, "Cannot run PCG graph batch '%ls': PCG engine subsystem is unavailable.", *GetPathName());
		return;
	}

	ExecutionQueue.Reset();
	for (int32 EntryIndex = 0; EntryIndex < Graphs.Num(); ++EntryIndex)
	{
		const FPCGUtilsGraphBatchEntry& Entry = Graphs[EntryIndex];
		if (!Entry.bEnabled)
		{
			continue;
		}

		FText Error;
		if (!ValidateEntry(Entry, EntryIndex, Error))
		{
			ExecutionStatus = FText::Format(LOCTEXT("ValidationFailedStatus", "Cannot run: {0}"), Error);
			UE_LOGF(LogPCG, Error, "%ls", *ExecutionStatus.ToString());
			ExecutionQueue.Reset();
			return;
		}

		ExecutionQueue.Add(Entry);
	}

	if (ExecutionQueue.IsEmpty())
	{
		ExecutionStatus = LOCTEXT("NothingToRunStatus", "Nothing to run: the batch has no enabled entries.");
		return;
	}

	bBatchRunning = true;
	bCancelRequested = false;
	bStopOnFailureForRun = bStopOnFailure;
	NextExecutionIndex = 0;
	CompletedGraphCount = 0;
	ActiveExecutionSource.Reset();

	UE_LOGF(LogPCG, Display, "Running PCG graph batch '%ls' (%d graph(s)).", *GetPathName(), ExecutionQueue.Num());
	ExecuteNextGraph();
}

void UPCGUtilsGraphBatch::CancelBatch()
{
	if (!bBatchRunning || bCancelRequested)
	{
		return;
	}

	bCancelRequested = true;
	ExecutionStatus = LOCTEXT("CancellingStatus", "Cancelling current graph...");

	if (UPCGDefaultExecutionSource* Source = ActiveExecutionSource.Get())
	{
		Source->GetExecutionState().Cancel();
	}
	else
	{
		FinishBatch(LOCTEXT("CancelledStatus", "Batch cancelled."));
	}
}

void UPCGUtilsGraphBatch::ExecuteNextGraph()
{
	if (!bBatchRunning)
	{
		return;
	}

	if (bCancelRequested)
	{
		FinishBatch(LOCTEXT("CancelledStatus", "Batch cancelled."));
		return;
	}

	if (!ExecutionQueue.IsValidIndex(NextExecutionIndex))
	{
		FinishBatch(FText::Format(
			LOCTEXT("CompletedStatus", "Completed {0} graph(s)."),
			FText::AsNumber(CompletedGraphCount)));
		return;
	}

	const FPCGUtilsGraphBatchEntry& Entry = ExecutionQueue[NextExecutionIndex];
	const FText EntryName = PCGUtilsGraphBatch::GetEntryName(Entry, NextExecutionIndex);
	ExecutionStatus = FText::Format(
		LOCTEXT("RunningStatus", "Running {0} of {1}: {2}"),
		FText::AsNumber(NextExecutionIndex + 1),
		FText::AsNumber(ExecutionQueue.Num()),
		EntryName);

	FPCGDefaultExecutionSourceParams Params;
	Params.GraphInterface = Entry.Graph;
	Params.Seed = Entry.Seed;
	Params.bFireAndForgetExecution = true;
	Params.GenerationCallback = FPCGOnEditorGenerationDone::CreateWeakLambda(
		this,
		[this](IPCGGraphExecutionSource* Source, const EPCGGenerationStatus Status)
		{
			OnGraphGenerationFinished(Source, Status);
		});

	ActiveExecutionSource = IPCGBaseSubsystem::CreateExecutionSource<UPCGDefaultExecutionSource>(Params);
	if (!ActiveExecutionSource.IsValid())
	{
		OnGraphGenerationFinished(nullptr, EPCGGenerationStatus::Aborted);
	}
}

void UPCGUtilsGraphBatch::OnGraphGenerationFinished(
	IPCGGraphExecutionSource* Source,
	const EPCGGenerationStatus Status)
{
	if (!bBatchRunning)
	{
		return;
	}

	if (ActiveExecutionSource.IsValid() && Source && Source != ActiveExecutionSource.Get())
	{
		return;
	}

	const FPCGUtilsGraphBatchEntry* CompletedEntry = ExecutionQueue.IsValidIndex(NextExecutionIndex)
		? &ExecutionQueue[NextExecutionIndex]
		: nullptr;
	const FText EntryName = CompletedEntry
		? PCGUtilsGraphBatch::GetEntryName(*CompletedEntry, NextExecutionIndex)
		: LOCTEXT("UnknownGraphName", "Unknown graph");

	ActiveExecutionSource.Reset();

	if (bCancelRequested)
	{
		FinishBatch(FText::Format(LOCTEXT("CancelledGraphStatus", "Batch cancelled while running {0}."), EntryName));
		return;
	}

	if (Status == EPCGGenerationStatus::Aborted)
	{
		UE_LOGF(LogPCG, Error, "PCG graph batch '%ls': graph '%ls' aborted.", *GetPathName(), *EntryName.ToString());
		if (bStopOnFailureForRun)
		{
			FinishBatch(FText::Format(LOCTEXT("StoppedOnFailureStatus", "Stopped: {0} aborted."), EntryName));
			return;
		}
	}
	else
	{
		++CompletedGraphCount;
	}

	++NextExecutionIndex;
	ExecuteNextGraph();
}

void UPCGUtilsGraphBatch::FinishBatch(const FText& FinalStatus)
{
	UE_LOGF(LogPCG, Display, "PCG graph batch '%ls': %ls", *GetPathName(), *FinalStatus.ToString());

	bBatchRunning = false;
	bCancelRequested = false;
	NextExecutionIndex = 0;
	ActiveExecutionSource.Reset();
	ExecutionQueue.Reset();
	ExecutionStatus = FinalStatus;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
