// Copyright Max Harris

#include "PCGUtilsAutoRegen.h"

#include "GameFramework/Actor.h"
#include "PCGActorBase.h"
#include "PCGComponent.h"

#if WITH_EDITOR
#include "ChangeTracking/PCGChangeTrackingRegistry.h"
#include "PCGModule.h"
#include "UObject/UObjectIterator.h"
#endif

namespace PCGUtilsAutoRegenInternal
{
	static bool GSuppressedForSession = false;

#if WITH_EDITOR
	static const FLazyName TrackerName(TEXT("PCGUtilsAutoRegenTracker"));

	/** One actor's owed regeneration. An edit made after a generation started is not in its result, so it stays owed. */
	struct FOwedRegen
	{
		TWeakObjectPtr<UPCGComponent> Source;
		FDelegateHandle StartHandle;
		FDelegateHandle DoneHandle;
		uint64 EditSerial = 0;
		uint64 SerialAtStart = MAX_uint64;
		TSet<TWeakObjectPtr<UPCGComponent>> Dependents;
	};

	static TMap<TWeakObjectPtr<const AActor>, FOwedRegen>& GetOwed()
	{
		static TMap<TWeakObjectPtr<const AActor>, FOwedRegen> Owed;
		return Owed;
	}

	static void OnSourceStarted(TWeakObjectPtr<const AActor> Key)
	{
		if (FOwedRegen* Entry = GetOwed().Find(Key))
		{
			Entry->SerialAtStart = Entry->EditSerial;
		}
	}

	static void OnSourceGenerated(TWeakObjectPtr<const AActor> Key)
	{
		FOwedRegen* Entry = GetOwed().Find(Key);
		if (!Entry || Entry->SerialAtStart != Entry->EditSerial)
		{
			return;
		}
		const TSet<TWeakObjectPtr<UPCGComponent>> Dependents = MoveTemp(Entry->Dependents);
		if (UPCGComponent* Source = Entry->Source.Get())
		{
			Source->OnPCGGraphStartGeneratingDelegate.Remove(Entry->StartHandle);
			Source->OnPCGGraphGeneratedDelegate.Remove(Entry->DoneHandle);
		}
		GetOwed().Remove(Key);
		// The engine's own cascade only follows graphs that read this actor's PCG output, so graphs that read its
		// components (a foliage graph reading the green spline) are caught up here.
		for (const TWeakObjectPtr<UPCGComponent>& Weak : Dependents)
		{
			if (UPCGComponent* Dependent = Weak.Get())
			{
				Dependent->Refresh();
			}
		}
	}

	/** Holds back the refresh the engine would start for an edit of a suppressed actor; its caches are already cleared. */
	class FAutoRegenTracker : public IPCGChangeTracker
	{
	public:
		explicit FAutoRegenTracker(FPCGTrackingManager* InOwner) : IPCGChangeTracker(InOwner) {}

		static TUniquePtr<IPCGChangeTracker> MakeInstance(FPCGTrackingManager* InOwner)
		{
			return MakeUnique<FAutoRegenTracker>(InOwner);
		}

		virtual bool ShouldSkipRefresh(const UObject* InChangedObject) override
		{
			const AActor* Actor = Cast<AActor>(InChangedObject);
			if (const UActorComponent* Component = Actor ? nullptr : Cast<UActorComponent>(InChangedObject))
			{
				Actor = Component->GetOwner();
			}
			const APCGActorBase* PCGActor = Cast<APCGActorBase>(Actor);
			if (!PCGActor || !FPCGUtilsAutoRegen::IsSuppressedFor(PCGActor))
			{
				return false;
			}
			// The actor's own generation or cleanup finishing reports the actor as changed too: that is not an edit,
			// and the engine must finish its dependency bookkeeping for it.
			TInlineComponentArray<UPCGComponent*, 2> PCGComponents;
			PCGActor->GetComponents(PCGComponents);
			for (UPCGComponent* PCGComponent : PCGComponents)
			{
				if (PCGComponent->IsGenerationInProgress() || PCGComponent->IsCleaningUp())
				{
					return false;
				}
			}
			FPCGUtilsAutoRegen::NoteSuppressedEdit(const_cast<APCGActorBase*>(PCGActor), /*bCaptureDependents=*/true);
			return true;
		}
	};
#endif
}

void FPCGUtilsAutoRegen::SetSuppressedForSession(bool bSuppressed)
{
	PCGUtilsAutoRegenInternal::GSuppressedForSession = bSuppressed;
}

bool FPCGUtilsAutoRegen::IsSuppressedForSession()
{
	return PCGUtilsAutoRegenInternal::GSuppressedForSession;
}

bool FPCGUtilsAutoRegen::IsSuppressedFor(const AActor* Actor)
{
#if WITH_EDITOR
	if (PCGUtilsAutoRegenInternal::GSuppressedForSession)
	{
		return true;
	}
	const APCGActorBase* PCGActor = Cast<APCGActorBase>(Actor);
	return PCGActor && PCGActor->bForceDisableComponentPCGAutoRegen;
#else
	return false;
#endif
}

void FPCGUtilsAutoRegen::NoteSuppressedEdit(AActor* Actor, bool bCaptureDependents)
{
#if WITH_EDITOR
	using namespace PCGUtilsAutoRegenInternal;
	if (!IsValid(Actor))
	{
		return;
	}
	const APCGActorBase* PCGActor = Cast<APCGActorBase>(Actor);
	UPCGComponent* Source = PCGActor ? PCGActor->PCGComponent.Get() : Actor->FindComponentByClass<UPCGComponent>();
	if (!Source)
	{
		return;
	}
	const TWeakObjectPtr<const AActor> Key(Actor);
	FOwedRegen* Entry = GetOwed().Find(Key);
	if (!Entry || Entry->Source.Get() != Source)
	{
		Entry = &GetOwed().Add(Key);
		Entry->Source = Source;
		Entry->StartHandle = Source->OnPCGGraphStartGeneratingDelegate.AddLambda([Key](UPCGComponent*) { OnSourceStarted(Key); });
		Entry->DoneHandle = Source->OnPCGGraphGeneratedDelegate.AddLambda([Key](UPCGComponent*) { OnSourceGenerated(Key); });
	}
	++Entry->EditSerial;
	if (bCaptureDependents)
	{
		const UWorld* World = Actor->GetWorld();
		for (TObjectIterator<UPCGComponent> It; It; ++It)
		{
			UPCGComponent* Component = *It;
			if (IsValid(Component) && Component != Source && !Component->IsTemplate() && Component->GetWorld() == World && Component->bDirtyGenerated)
			{
				Entry->Dependents.Add(Component);
			}
		}
	}
#endif
}

bool FPCGUtilsAutoRegen::IsRegenOwed(const AActor* Actor)
{
#if WITH_EDITOR
	return Actor && PCGUtilsAutoRegenInternal::GetOwed().Contains(TWeakObjectPtr<const AActor>(Actor));
#else
	return false;
#endif
}

#if WITH_EDITOR
void FPCGUtilsAutoRegen::RegisterChangeTracker()
{
	using namespace PCGUtilsAutoRegenInternal;
	FPCGModule::GetMutableChangeTrackingRegistry().RegisterTrackerFactory(TrackerName.Resolve(), FPCGCreateTrackerInstance::CreateStatic(&FAutoRegenTracker::MakeInstance));
}

void FPCGUtilsAutoRegen::UnregisterChangeTracker()
{
	using namespace PCGUtilsAutoRegenInternal;
	if (FPCGModule::IsPCGModuleLoaded())
	{
		FPCGModule::GetMutableChangeTrackingRegistry().UnregisterTrackerFactory(TrackerName.Resolve());
	}
}
#endif
