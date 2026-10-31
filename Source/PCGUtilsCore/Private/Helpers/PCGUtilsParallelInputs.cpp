// Copyright Max Harris

#include "Helpers/PCGUtilsParallelInputs.h"

#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectHash.h"

#include <atomic>

namespace PCGUtilsParallelInputs
{
	namespace
	{
		TAutoConsoleVariable<int32> CVarParallelInputs(
			TEXT("pcgutils.ParallelInputs"),
			1,
			TEXT("1 (default): PCGUtils nodes may process independent inputs in parallel. 0: every node processes its ")
			TEXT("inputs one at a time. Use it to check whether a problem comes from the parallel path, and to time ")
			TEXT("serial against parallel. A single node can also be forced serial with its Disable Parallel setting."),
			ECVF_Default);

		thread_local FAllocationScope* GCurrentScope = nullptr;

		std::atomic<int32> GParallelRunCount{0};
	}

	FAllocationScope::FAllocationScope(FPCGContext* InContext)
		: Context(InContext)
	{
		check(IsInGameThread());
	}

	FAllocationScope::~FAllocationScope()
	{
		check(IsInGameThread());
		// Same clearing as PCGGraphExecutor::ClearAsyncFlags: the object and everything it outers, so the garbage
		// collector tracks them like any game-thread object from now on (kept alive by the outputs that use them).
		for (UObject* Object : Objects)
		{
			if (Object && Object->HasAnyInternalFlags(EInternalObjectFlags::Async))
			{
				Object->ClearInternalFlags(EInternalObjectFlags::Async);
				ForEachObjectWithOuter(Object, [](UObject* SubObject)
				{
					SubObject->ClearInternalFlags(EInternalObjectFlags::Async);
				}, EGetObjectsFlags::IncludeNestedObjects);
			}
		}
	}

	void FAllocationScope::Record(UObject* Object)
	{
		FScopeLock ScopeLock(&Lock);
		Objects.Add(Object);
	}

	int32 FAllocationScope::NumRecorded() const
	{
		FScopeLock ScopeLock(&Lock);
		return Objects.Num();
	}

	FAllocationScope* GetCurrentScope()
	{
		return GCurrentScope;
	}

	FBodyScope::FBodyScope(FAllocationScope* Scope)
		: Previous(GCurrentScope)
	{
		GCurrentScope = Scope;
	}

	FBodyScope::~FBodyScope()
	{
		GCurrentScope = Previous;
	}

	bool IsEnabledGlobally()
	{
		return CVarParallelInputs.GetValueOnAnyThread() != 0;
	}

	bool ForEach(FPCGContext* Context, int32 Num, bool bParallel, TFunctionRef<void(int32 Index)> Body)
	{
		if (!bParallel || Num < 2 || !IsInGameThread())
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Body(Index);
			}
			return false;
		}

		++GParallelRunCount;
		FAllocationScope Scope(Context);
		ParallelFor(Num, [&Scope, &Body](int32 Index)
		{
			FBodyScope BodyScope(&Scope);
			Body(Index);
		}, EParallelForFlags::Unbalanced);
		return true;
	}

	int32 GetParallelRunCount()
	{
		return GParallelRunCount.load();
	}
}
