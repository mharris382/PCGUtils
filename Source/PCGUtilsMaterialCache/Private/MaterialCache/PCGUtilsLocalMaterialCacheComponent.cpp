#include "MaterialCache/PCGUtilsLocalMaterialCacheComponent.h"

#include "MaterialCache/PCGUtilsComponentIdentity.h"
#include "MaterialCache/PCGUtilsMaterialVariantCanonicalizer.h"
#include "MaterialCache/PCGUtilsMaterialParameterApplication.h"
#include "PCGUtilsMaterialCacheModule.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	FPCGUtilsLocalMaterialBindingKey MakeBindingKey(FName OwnerComponentKey, FName BindingName, FName VariantName)
	{
		FPCGUtilsLocalMaterialBindingKey Key;
		Key.OwnerComponentKey = OwnerComponentKey;
		Key.BindingName = BindingName;
		Key.VariantName = VariantName;
		return Key;
	}
}

void UPCGUtilsLocalMaterialCacheComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ClearAllLocalMaterials();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UPCGUtilsLocalMaterialCacheComponent::ValidateOwnerComponent(const UActorComponent* OwnerComponent, FText& OutError) const
{
	if (!OwnerComponent)
	{
		OutError = NSLOCTEXT("PCGUtilsLocalMaterialCacheComponent", "NullOwner", "Local material resolution failed: owner component is null.");
		return false;
	}

	const AActor* ThisActor = GetOwner();
	if (!ThisActor || OwnerComponent->GetOwner() != ThisActor)
	{
		OutError = FText::Format(
			NSLOCTEXT("PCGUtilsLocalMaterialCacheComponent", "WrongActor", "Local material resolution failed: component '{0}' belongs to a different actor than this local material cache component. Cross-actor local ownership is not supported."),
			FText::FromString(OwnerComponent->GetName()));
		return false;
	}

	return true;
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::ResolveLocalMaterial(
	UActorComponent* OwnerComponent,
	FName BindingName,
	FName VariantName,
	const FPCGUtilsMaterialVariantRequest& InitialConfiguration)
{
	checkf(IsInGameThread(), TEXT("UPCGUtilsLocalMaterialCacheComponent::ResolveLocalMaterial must be called from the game thread."));

	FText ValidationError;
	if (!ValidateOwnerComponent(OwnerComponent, ValidationError))
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *ValidationError.ToString());
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
	{
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	return ResolveLocalMaterialInternal(OwnerComponent, OwnerComponentKey, BindingName, VariantName, InitialConfiguration);
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::ResolveLocalMaterialFromComponentPath(
	const FSoftObjectPath& OwnerComponentPath,
	FName BindingName,
	FName VariantName,
	const FPCGUtilsMaterialVariantRequest& InitialConfiguration)
{
	checkf(IsInGameThread(), TEXT("UPCGUtilsLocalMaterialCacheComponent::ResolveLocalMaterialFromComponentPath must be called from the game thread."));

	UActorComponent* OwnerComponent = nullptr;
	FText ResolveError;
	if (!PCGUtilsMaterialCache::TryResolveComponent(OwnerComponentPath, OwnerComponent, &ResolveError))
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *ResolveError.ToString());
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	// Delegates to the direct-reference overload so both paths run identical validation
	// and produce identical owner keys - callers never see a different result merely
	// because they supplied a soft path instead of a direct reference.
	return ResolveLocalMaterial(OwnerComponent, BindingName, VariantName, InitialConfiguration);
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::ResolveLocalMaterialInternal(
	UActorComponent* OwnerComponent,
	FName OwnerComponentKey,
	FName BindingName,
	FName VariantName,
	const FPCGUtilsMaterialVariantRequest& InitialConfiguration)
{
	RemoveInvalidComponentEntries();

	UMaterialInterface* ParentMaterial = InitialConfiguration.ParentMaterial.LoadSynchronous();
	if (!ParentMaterial)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("Local material resolution failed: parent material '%s' could not be resolved/loaded."),
			*InitialConfiguration.ParentMaterial.ToSoftObjectPath().ToString());
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	FPCGUtilsMaterialVariantKey NewInitKey;
	FText CanonicalizeError;
	if (!PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(InitialConfiguration, NewInitKey, CanonicalizeError))
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"), *CanonicalizeError.ToString());
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	const FPCGUtilsLocalMaterialBindingKey BindingKey = MakeBindingKey(OwnerComponentKey, BindingName, VariantName);

	if (FPCGUtilsLocalMaterialEntry* Existing = LocalEntries.Find(BindingKey))
	{
		if (Existing->InitializationKey == NewInitKey)
		{
			// Same initialization: reuse as-is. Do not reapply initialization values -
			// that would clobber any gameplay-driven parameter changes made since.
			Statistics.LocalCacheHits++;
			return Existing->Material;
		}

		// Initialization changed: create a replacement rather than mutating the existing
		// MID, so stale gameplay state can never mix unpredictably with a new base config.
		UMaterialInstanceDynamic* ReplacementMID = UMaterialInstanceDynamic::Create(ParentMaterial, this);
		FText ApplyError;
		if (!ReplacementMID || !PCGUtilsMaterialCache::TryApplyMaterialParameterOverrides(ReplacementMID, ParentMaterial, InitialConfiguration.ParameterOverrides, ApplyError))
		{
			UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("Local material replacement failed, keeping existing entry: %s"),
				ReplacementMID ? *ApplyError.ToString() : TEXT("could not create dynamic material instance."));
			Statistics.FailedComponentResolutions++;
			return nullptr;
		}

		Existing->Material = ReplacementMID;
		Existing->InitializationKey = NewInitKey;
		Existing->OwnerComponent = OwnerComponent;
		Statistics.Replacements++;
		return ReplacementMID;
	}

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(ParentMaterial, this);
	FText ApplyError;
	if (!NewMID || !PCGUtilsMaterialCache::TryApplyMaterialParameterOverrides(NewMID, ParentMaterial, InitialConfiguration.ParameterOverrides, ApplyError))
	{
		UE_LOG(LogPCGUtilsMaterialCache, Warning, TEXT("%s"),
			NewMID ? *ApplyError.ToString() : TEXT("Local material creation failed: could not create dynamic material instance."));
		Statistics.FailedComponentResolutions++;
		return nullptr;
	}

	FPCGUtilsLocalMaterialEntry NewEntry;
	NewEntry.Material = NewMID;
	NewEntry.InitializationKey = NewInitKey;
	NewEntry.OwnerComponent = OwnerComponent;
	LocalEntries.Add(BindingKey, NewEntry);

	Statistics.LocalCacheMisses++;
	return NewMID;
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterial(const UActorComponent* OwnerComponent, FName BindingName, FName VariantName) const
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
	{
		return nullptr;
	}
	return GetLocalMaterialByOwnerKey(OwnerComponentKey, BindingName, VariantName);
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialFromComponentPath(const FSoftObjectPath& OwnerComponentPath, FName BindingName, FName VariantName) const
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponentPath, OwnerComponentKey))
	{
		return nullptr;
	}
	return GetLocalMaterialByOwnerKey(OwnerComponentKey, BindingName, VariantName);
}

UMaterialInstanceDynamic* UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialByOwnerKey(FName OwnerComponentKey, FName BindingName, FName VariantName) const
{
	const FPCGUtilsLocalMaterialEntry* Entry = LocalEntries.Find(MakeBindingKey(OwnerComponentKey, BindingName, VariantName));
	return Entry ? Entry->Material : nullptr;
}

TArray<UMaterialInstanceDynamic*> UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialsForComponent(const UActorComponent* OwnerComponent) const
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
	{
		return {};
	}
	return GetLocalMaterialsForOwnerKey(OwnerComponentKey);
}

TArray<UMaterialInstanceDynamic*> UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialsForComponentPath(const FSoftObjectPath& OwnerComponentPath) const
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponentPath, OwnerComponentKey))
	{
		return {};
	}
	return GetLocalMaterialsForOwnerKey(OwnerComponentKey);
}

TArray<UMaterialInstanceDynamic*> UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialsForOwnerKey(FName OwnerComponentKey) const
{
	TArray<UMaterialInstanceDynamic*> Result;
	for (const TPair<FPCGUtilsLocalMaterialBindingKey, FPCGUtilsLocalMaterialEntry>& Pair : LocalEntries)
	{
		if (Pair.Key.OwnerComponentKey == OwnerComponentKey && Pair.Value.Material)
		{
			Result.Add(Pair.Value.Material);
		}
	}
	return Result;
}

TArray<UMaterialInstanceDynamic*> UPCGUtilsLocalMaterialCacheComponent::GetAllLocalMaterials() const
{
	TArray<UMaterialInstanceDynamic*> Result;
	Result.Reserve(LocalEntries.Num());
	for (const TPair<FPCGUtilsLocalMaterialBindingKey, FPCGUtilsLocalMaterialEntry>& Pair : LocalEntries)
	{
		if (Pair.Value.Material)
		{
			Result.Add(Pair.Value.Material);
		}
	}
	return Result;
}

bool UPCGUtilsLocalMaterialCacheComponent::RemoveLocalMaterial(const UActorComponent* OwnerComponent, FName BindingName, FName VariantName)
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
	{
		return false;
	}
	return LocalEntries.Remove(MakeBindingKey(OwnerComponentKey, BindingName, VariantName)) > 0;
}

bool UPCGUtilsLocalMaterialCacheComponent::RemoveLocalMaterialFromComponentPath(const FSoftObjectPath& OwnerComponentPath, FName BindingName, FName VariantName)
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponentPath, OwnerComponentKey))
	{
		return false;
	}
	return LocalEntries.Remove(MakeBindingKey(OwnerComponentKey, BindingName, VariantName)) > 0;
}

int32 UPCGUtilsLocalMaterialCacheComponent::RemoveAllLocalMaterialsForComponent(const UActorComponent* OwnerComponent)
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
	{
		return 0;
	}
	return RemoveAllLocalMaterialsForOwnerKey(OwnerComponentKey);
}

int32 UPCGUtilsLocalMaterialCacheComponent::RemoveAllLocalMaterialsForComponentPath(const FSoftObjectPath& OwnerComponentPath)
{
	FName OwnerComponentKey;
	if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponentPath, OwnerComponentKey))
	{
		return 0;
	}
	return RemoveAllLocalMaterialsForOwnerKey(OwnerComponentKey);
}

int32 UPCGUtilsLocalMaterialCacheComponent::RemoveAllLocalMaterialsForOwnerKey(FName OwnerComponentKey)
{
	int32 NumRemoved = 0;
	for (auto It = LocalEntries.CreateIterator(); It; ++It)
	{
		if (It.Key().OwnerComponentKey == OwnerComponentKey)
		{
			It.RemoveCurrent();
			++NumRemoved;
		}
	}
	return NumRemoved;
}

void UPCGUtilsLocalMaterialCacheComponent::ClearAllLocalMaterials()
{
	LocalEntries.Empty();
}

int32 UPCGUtilsLocalMaterialCacheComponent::RemoveInvalidComponentEntries()
{
	int32 NumRemoved = 0;
	for (auto It = LocalEntries.CreateIterator(); It; ++It)
	{
		if (!It.Value().OwnerComponent.IsValid())
		{
			It.RemoveCurrent();
			++NumRemoved;
		}
	}
	Statistics.InvalidEntriesRemoved += NumRemoved;
	return NumRemoved;
}

FPCGUtilsLocalMaterialCacheStatistics UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterialCacheStatistics() const
{
	// NumLocalMaterials, NumLocalBindings, and CountsByOwnerComponent reflect current live
	// state (recomputed here) rather than being incrementally tracked, so they are always
	// correct after removals without needing matching decrement logic at every removal
	// call site. The other counters are cumulative for this component's lifetime.
	FPCGUtilsLocalMaterialCacheStatistics Result = Statistics;
	Result.NumLocalMaterials = LocalEntries.Num();

	TMap<FName, TSet<FName>> OwnerToBindingNames;
	TMap<FName, int32> CountsByOwner;
	for (const TPair<FPCGUtilsLocalMaterialBindingKey, FPCGUtilsLocalMaterialEntry>& Pair : LocalEntries)
	{
		OwnerToBindingNames.FindOrAdd(Pair.Key.OwnerComponentKey).Add(Pair.Key.BindingName);
		CountsByOwner.FindOrAdd(Pair.Key.OwnerComponentKey)++;
	}

	int32 NumBindings = 0;
	for (const TPair<FName, TSet<FName>>& OwnerPair : OwnerToBindingNames)
	{
		NumBindings += OwnerPair.Value.Num();
	}
	Result.NumLocalBindings = NumBindings;
	Result.CountsByOwnerComponent = MoveTemp(CountsByOwner);

	return Result;
}

void UPCGUtilsLocalMaterialCacheComponent::DumpLocalMaterialCache() const
{
	UE_LOG(LogPCGUtilsMaterialCache, Log, TEXT("Local Material Cache Dump (%s, CacheName=%s): %d entries"),
		*GetName(), *CacheName.ToString(), LocalEntries.Num());

	for (const TPair<FPCGUtilsLocalMaterialBindingKey, FPCGUtilsLocalMaterialEntry>& Pair : LocalEntries)
	{
		UE_LOG(LogPCGUtilsMaterialCache, Log, TEXT("  Owner=%s Binding=%s Variant=%s Parent=%s ComponentValid=%s"),
			*Pair.Key.OwnerComponentKey.ToString(),
			*Pair.Key.BindingName.ToString(),
			*Pair.Key.VariantName.ToString(),
			*Pair.Value.InitializationKey.ParentMaterialPath.ToString(),
			Pair.Value.OwnerComponent.IsValid() ? TEXT("true") : TEXT("false"));
	}
}
