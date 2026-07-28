#include "MaterialCache/PCGUtilsLocalMaterialCacheLookup.h"

#include "MaterialCache/PCGUtilsLocalMaterialCacheComponent.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#define LOCTEXT_NAMESPACE "PCGUtilsLocalMaterialCacheLookup"

UPCGUtilsLocalMaterialCacheComponent* PCGUtilsMaterialCache::FindLocalMaterialCacheComponentOnActor(AActor* Actor, FName CacheName, FText& OutError)
{
	if (!Actor)
	{
		OutError = LOCTEXT("NullActor", "Cannot find a local material cache component: actor is null.");
		return nullptr;
	}

	TArray<UPCGUtilsLocalMaterialCacheComponent*> Candidates;
	Actor->GetComponents<UPCGUtilsLocalMaterialCacheComponent>(Candidates);

	if (CacheName.IsNone())
	{
		if (Candidates.IsEmpty())
		{
			OutError = FText::Format(
				LOCTEXT("NoCacheComponent", "Actor '{0}' has no UPCGUtilsLocalMaterialCacheComponent. Local material ownership requires the actor to explicitly contain one."),
				FText::FromString(Actor->GetName()));
			return nullptr;
		}

		if (Candidates.Num() > 1)
		{
			OutError = FText::Format(
				LOCTEXT("AmbiguousCacheComponent", "Actor '{0}' has {1} local material cache components. Specify CacheName to disambiguate."),
				FText::FromString(Actor->GetName()), FText::AsNumber(Candidates.Num()));
			return nullptr;
		}

		return Candidates[0];
	}

	UPCGUtilsLocalMaterialCacheComponent* Match = nullptr;
	int32 NumMatches = 0;
	for (UPCGUtilsLocalMaterialCacheComponent* Candidate : Candidates)
	{
		if (Candidate && Candidate->CacheName == CacheName)
		{
			Match = Candidate;
			++NumMatches;
		}
	}

	if (NumMatches == 0)
	{
		OutError = FText::Format(
			LOCTEXT("NoMatchingCacheName", "Actor '{0}' has no local material cache component with CacheName '{1}'."),
			FText::FromString(Actor->GetName()), FText::FromName(CacheName));
		return nullptr;
	}

	if (NumMatches > 1)
	{
		OutError = FText::Format(
			LOCTEXT("DuplicateCacheName", "Actor '{0}' has {1} local material cache components sharing CacheName '{2}'. CacheName values must be unique per actor."),
			FText::FromString(Actor->GetName()), FText::AsNumber(NumMatches), FText::FromName(CacheName));
		return nullptr;
	}

	return Match;
}

UPCGUtilsLocalMaterialCacheComponent* PCGUtilsMaterialCache::FindLocalMaterialCacheComponentFromContext(UObject* Context, FName CacheName, FText& OutError)
{
	if (UPCGUtilsLocalMaterialCacheComponent* AsCacheComponent = Cast<UPCGUtilsLocalMaterialCacheComponent>(Context))
	{
		return AsCacheComponent;
	}

	if (UActorComponent* AsComponent = Cast<UActorComponent>(Context))
	{
		return FindLocalMaterialCacheComponentOnActor(AsComponent->GetOwner(), CacheName, OutError);
	}

	if (AActor* AsActor = Cast<AActor>(Context))
	{
		return FindLocalMaterialCacheComponentOnActor(AsActor, CacheName, OutError);
	}

	OutError = FText::Format(
		LOCTEXT("UnsupportedContext", "'{0}' is not a UPCGUtilsLocalMaterialCacheComponent, UActorComponent, or AActor - cannot find a local material cache component from it."),
		FText::FromString(Context ? Context->GetName() : TEXT("null")));
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
