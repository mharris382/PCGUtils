// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * The one gate for PCG regeneration caused by edits. An edit is suppressed when the session switch is on (an editor tool
 * such as a route mode sets it; never saved) or the owning APCGActorBase has bForceDisableComponentPCGAutoRegen.
 *
 * It covers both ways an edit regenerates PCG: PCGUtils components calling UPCGUtilsHelpers::TryRefreshPCGGeneration, and
 * the engine's change tracking refreshing OTHER actors whose graphs read the edited actor, which no flag on the edited
 * actor stops. A suppressed edit is never lost: the engine still clears the caches, and the actor is recorded as owing a
 * regeneration. Explicit generate calls are not gated; when the actor next generates, the components its edits left
 * dirty refresh too.
 */
class PCGUTILS_API FPCGUtilsAutoRegen
{
public:
	/** Turns suppression on or off for every APCGActorBase, without touching any actor. Not saved. */
	static void SetSuppressedForSession(bool bSuppressed);
	static bool IsSuppressedForSession();

	/** True when an edit of this actor must not regenerate PCG. Always false outside the editor. */
	static bool IsSuppressedFor(const AActor* Actor);

	/** Records that an edit of the actor was suppressed. With bCaptureDependents, also the components it left dirty. */
	static void NoteSuppressedEdit(AActor* Actor, bool bCaptureDependents = false);

	/** True while the actor has a suppressed edit that its PCG has not generated yet. */
	static bool IsRegenOwed(const AActor* Actor);

#if WITH_EDITOR
	/** Module startup and shutdown: the engine change tracker that holds back refreshes for suppressed actors. */
	static void RegisterChangeTracker();
	static void UnregisterChangeTracker();
#endif
};
