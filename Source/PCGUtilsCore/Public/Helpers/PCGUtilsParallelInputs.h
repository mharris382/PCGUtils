// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "UObject/GarbageCollection.h"

/**
 * Running independent per-input work in parallel from a PCG element, and allocating UObjects safely while doing it.
 *
 * Why this exists: FPCGContext::NewObject_AnyThread is safe from *one* worker at a time (an element that runs off
 * the game thread), but not from several workers spawned by one element: it adds every object to an unlocked set
 * on the context, and it ensures the element is not marked as running on the main thread. Inside a ParallelFor
 * started by a game-thread element both assumptions break.
 *
 * Rules for code that can run inside a parallel body (process operations and everything they call):
 *  - Allocate with PCGUtilsAsyncObjects::NewObject<T>(Context, ...), never FPCGContext::NewObject_AnyThread.
 *    Outside a parallel body it calls FPCGContext::NewObject_AnyThread, so it is always the right call.
 *  - Log with an explicit context (PCGLog::...(Text, Context)). FPCGContext::GetContextForThread() is null on workers.
 *  - Never touch shared mutable state. Reading actors and other UObjects is fine: the game thread is blocked
 *    inside the ParallelFor for the whole run, so nothing changes them and garbage collection cannot run.
 *
 * See Docs/PCGUtilsDynMesh/ParallelInputs.md.
 */
namespace PCGUtilsParallelInputs
{
	/**
	 * Records objects created by workers so their Async flag can be cleared on the game thread once the parallel
	 * run ends. Objects created off the game thread carry EInternalObjectFlags::Async, and the garbage collector
	 * never collects them while it is set; the PCG executor clears it only for objects in the context's own list,
	 * which workers cannot add to. The engine's Execute Blueprint element does the same clearing.
	 */
	class PCGUTILSCORE_API FAllocationScope
	{
	public:
		explicit FAllocationScope(FPCGContext* InContext);
		~FAllocationScope();

		FAllocationScope(const FAllocationScope&) = delete;
		FAllocationScope& operator=(const FAllocationScope&) = delete;

		void Record(UObject* Object);

		/** Number of objects recorded from workers so far. For tests. */
		int32 NumRecorded() const;

	private:
		FPCGContext* Context = nullptr;
		mutable FCriticalSection Lock;
		TArray<UObject*> Objects;
	};

	/** The allocation scope of the parallel body running on this thread, or null. */
	PCGUTILSCORE_API FAllocationScope* GetCurrentScope();

	/** Sets the current thread's scope for one body call, restoring the previous one afterwards. */
	struct PCGUTILSCORE_API FBodyScope
	{
		explicit FBodyScope(FAllocationScope* Scope);
		~FBodyScope();

	private:
		FAllocationScope* Previous = nullptr;
	};

	/** pcgutils.ParallelInputs (default 1): 0 forces every node to process its inputs one at a time. */
	PCGUTILSCORE_API bool IsEnabledGlobally();

	/**
	 * Runs Body(Index) for Index in [0, Num). In parallel (ParallelFor, Unbalanced) when bParallel is true, Num is at
	 * least 2 and the caller is on the game thread; otherwise in order on the calling thread. Returns whether it
	 * ran in parallel. The caller decides bParallel from its own gates (settings, node and operation opt-outs, and
	 * IsEnabledGlobally()).
	 *
	 * Only the game thread may start a parallel run, because only there can the Async flags of worker-created
	 * objects be cleared afterwards (see FAllocationScope). An element already running off the game thread runs
	 * the body serially, allocating through FPCGContext::NewObject_AnyThread as usual.
	 */
	PCGUTILSCORE_API bool ForEach(FPCGContext* Context, int32 Num, bool bParallel, TFunctionRef<void(int32 Index)> Body);

	/** Number of parallel runs started by ForEach since startup. Lets tests tell whether a node took the parallel path. */
	PCGUTILSCORE_API int32 GetParallelRunCount();
}

namespace PCGUtilsAsyncObjects
{
	/**
	 * Allocate a UObject from code that may run inside a PCGUtilsParallelInputs::ForEach body. On a worker inside
	 * a parallel run, it creates the object under a GC guard and records it with the run's scope. Everywhere else
	 * it is exactly FPCGContext::NewObject_AnyThread.
	 */
	template<class T, typename... Args>
	T* NewObject(FPCGContext* Context, Args&&... InArgs)
	{
		if (!IsInGameThread())
		{
			if (PCGUtilsParallelInputs::FAllocationScope* Scope = PCGUtilsParallelInputs::GetCurrentScope())
			{
				T* Object = nullptr;
				{
					FGCScopeGuard Guard;
					Object = ::NewObject<T>(Forward<Args>(InArgs)...);
				}
				check(Object);
				Scope->Record(Object);
				return Object;
			}
		}
		return FPCGContext::NewObject_AnyThread<T>(Context, Forward<Args>(InArgs)...);
	}
}
