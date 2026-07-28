#include "Elements/PCGResolveMaterialVariants.h"

#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "MaterialCache/PCGUtilsMaterialVariantCacheSubsystem.h"
#include "MaterialCache/PCGUtilsMaterialVariantCanonicalizer.h"
#include "MaterialCache/PCGUtilsComponentIdentity.h"
#include "MaterialCache/PCGUtilsLocalMaterialCacheComponent.h"
#include "MaterialCache/PCGUtilsLocalMaterialCacheLookup.h"
#include "PCGUtilsMaterialCacheModule.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "PCGResolveMaterialVariants"

// ── Settings ──────────────────────────────────────────────────────────────────

UPCGResolveMaterialVariantsSettings::UPCGResolveMaterialVariantsSettings()
{
	OutputMaterialAttribute = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyOutputSelector>(TEXT("ResolvedMaterial"));
}

#if WITH_EDITOR
FText UPCGResolveMaterialVariantsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Resolve Material Variants");
}

FText UPCGResolveMaterialVariantsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Resolves a material variant per input entry - either a shared immutable variant through the global material cache, or a mutable per-component variant through a local material cache component - deduplicating identical requests, and writes a reference to the result onto an output attribute.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGResolveMaterialVariantsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace_GetRef(TEXT("In"), EPCGDataType::PointOrParam, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true).SetRequiredPin();
	return Properties;
}

TArray<FPCGPinProperties> UPCGResolveMaterialVariantsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(TEXT("Out"), EPCGDataType::PointOrParam, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true);
	return Properties;
}

FPCGElementPtr UPCGResolveMaterialVariantsSettings::CreateElement() const
{
	return MakeShared<FPCGResolveMaterialVariantsElement>();
}

// ── Element ───────────────────────────────────────────────────────────────────

namespace
{
	template <typename T>
	T GetBroadcastValue(const TArray<T>& Values, int32 Index, const T& DefaultValue)
	{
		if (Values.IsEmpty())
		{
			return DefaultValue;
		}
		if (Values.Num() == 1)
		{
			// Data-domain (single, shared) value - reused for every entry.
			return Values[0];
		}
		return Values.IsValidIndex(Index) ? Values[Index] : DefaultValue;
	}

	/** One input data's worth of logical entries, carried from request-building through to output assignment. */
	struct FPCGResolveMaterialVariantsEntryBundle
	{
		UPCGData* OutputData = nullptr;
		FPCGTaggedData SourceTaggedData;

		// Per-entry (size == logical entry count for this input). Built once, in Phase A,
		// used by whichever cache-scope branch runs.
		TArray<FPCGUtilsMaterialVariantRequest> Requests;
		TArray<FPCGUtilsMaterialVariantKey> Keys;
		TArray<bool> bKeyValid;

		// Local Component scope only.
		TArray<UActorComponent*> ResolvedOwnerComponents;
		TArray<UPCGUtilsLocalMaterialCacheComponent*> ResolvedCacheComponents;
		TArray<FName> ResolvedOwnerKeys;
		TArray<FName> ResolvedVariantNames;
		TArray<bool> bOwnerValid;

		// Filled by the active scope's resolution phase; read uniformly by output assignment.
		TArray<TObjectPtr<UMaterialInterface>> ResolvedMaterials;
	};

	/** Node-execution-local grouping identity for Local Component mode: see spec section 15 (registry lookup identity). */
	struct FPCGLocalGroupKey
	{
		UPCGUtilsLocalMaterialCacheComponent* CacheComponent = nullptr;
		FName OwnerComponentKey;
		FName BindingName;
		FName VariantName;

		bool operator==(const FPCGLocalGroupKey& Other) const
		{
			return CacheComponent == Other.CacheComponent
				&& OwnerComponentKey == Other.OwnerComponentKey
				&& BindingName == Other.BindingName
				&& VariantName == Other.VariantName;
		}

		friend uint32 GetTypeHash(const FPCGLocalGroupKey& Key)
		{
			uint32 Hash = GetTypeHash(Key.CacheComponent);
			Hash = HashCombine(Hash, GetTypeHash(Key.OwnerComponentKey));
			Hash = HashCombine(Hash, GetTypeHash(Key.BindingName));
			Hash = HashCombine(Hash, GetTypeHash(Key.VariantName));
			return Hash;
		}
	};

	struct FPCGLocalGroupData
	{
		FPCGUtilsMaterialVariantKey FirstInitializationKey;
		bool bHasFirstKey = false;
		bool bConflict = false;
		int32 RepresentativeBundleIndex = INDEX_NONE;
		int32 RepresentativeEntryIndex = INDEX_NONE;
		TArray<TPair<int32, int32>> EntryLocations; // (BundleIndex, EntryIndex)
		UMaterialInstanceDynamic* ResolvedMaterial = nullptr;
		bool bResolved = false;
	};
}

bool FPCGResolveMaterialVariantsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGResolveMaterialVariantsElement::ExecuteInternal);

	const UPCGResolveMaterialVariantsSettings* Settings = Context->GetInputSettings<UPCGResolveMaterialVariantsSettings>();
	check(Settings);

	const bool bIsLocalScope = (Settings->CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent);

	if (bIsLocalScope && Settings->BindingName.IsNone())
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoBindingName", "Resolve Material Variants: Local Component mode requires a non-empty Binding Name."));
		return true;
	}

	UWorld* World = Context->ExecutionSource.IsValid() ? Context->ExecutionSource->GetExecutionState().GetWorld() : nullptr;

	UPCGUtilsMaterialVariantCacheSubsystem* GlobalSubsystem = nullptr;
	if (!bIsLocalScope)
	{
		GlobalSubsystem = World ? World->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>() : nullptr;
		if (!GlobalSubsystem)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoSubsystem", "Resolve Material Variants: no valid world / material cache subsystem - cannot resolve any material variants."));
			return true;
		}
	}
	else if (!World)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoWorld", "Resolve Material Variants: no valid world - cannot resolve any local material variants."));
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(FName(TEXT("In")));

	// ── Phase A (worker-compatible): build, canonicalize, and locally deduplicate requests. ──

	TArray<FPCGResolveMaterialVariantsEntryBundle> EntryBundles;
	EntryBundles.Reserve(Inputs.Num());

	TMap<FPCGUtilsMaterialVariantKey, FPCGUtilsMaterialVariantRequest> UniqueGlobalRequests;

	int32 TotalInputEntries = 0;
	int32 TotalCanonicalizeFailures = 0;

	for (const FPCGTaggedData& InputTaggedData : Inputs)
	{
		const UPCGData* InputData = InputTaggedData.Data;
		if (!InputData)
		{
			continue;
		}

		const int32 NumEntries = (Cast<const UPCGBasePointData>(InputData) != nullptr)
			? CastChecked<const UPCGBasePointData>(InputData)->GetNumPoints()
			: 1;

		if (NumEntries <= 0)
		{
			continue;
		}

		TArray<FSoftObjectPath> ParentPaths;
		if (!PCGAttributeAccessorHelpers::ExtractAllValues<FSoftObjectPath>(InputData, Settings->ParentMaterialSource, ParentPaths, Context))
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(
				LOCTEXT("NoParentSource", "Resolve Material Variants: could not read parent material from '{0}' - skipping this input."),
				FText::FromString(Settings->ParentMaterialSource.ToString())));
			continue;
		}

		struct FBindingValues
		{
			TArray<float> Scalars;
			TArray<FVector4> Vectors;
			TArray<FSoftObjectPath> Textures;
		};

		TArray<FBindingValues> BindingValues;
		BindingValues.SetNum(Settings->ParameterBindings.Num());

		for (int32 BindingIndex = 0; BindingIndex < Settings->ParameterBindings.Num(); ++BindingIndex)
		{
			const FPCGUtilsMaterialParameterBinding& Binding = Settings->ParameterBindings[BindingIndex];
			switch (Binding.Type)
			{
			case EPCGUtilsMaterialParameterType::Scalar:
				PCGAttributeAccessorHelpers::ExtractAllValues<float>(InputData, Binding.Source, BindingValues[BindingIndex].Scalars, Context);
				break;
			case EPCGUtilsMaterialParameterType::Vector:
				PCGAttributeAccessorHelpers::ExtractAllValues<FVector4>(InputData, Binding.Source, BindingValues[BindingIndex].Vectors, Context);
				break;
			case EPCGUtilsMaterialParameterType::Texture:
				PCGAttributeAccessorHelpers::ExtractAllValues<FSoftObjectPath>(InputData, Binding.Source, BindingValues[BindingIndex].Textures, Context);
				break;
			}
		}

		TArray<FSoftObjectPath> OwnerPaths;
		TArray<FName> VariantNames;
		if (bIsLocalScope)
		{
			if (!PCGAttributeAccessorHelpers::ExtractAllValues<FSoftObjectPath>(InputData, Settings->OwnerComponentSource, OwnerPaths, Context))
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(
					LOCTEXT("NoOwnerSource", "Resolve Material Variants: could not read owner component from '{0}' - skipping this input."),
					FText::FromString(Settings->OwnerComponentSource.ToString())));
				continue;
			}

			if (Settings->bUseVariantNameSource)
			{
				PCGAttributeAccessorHelpers::ExtractAllValues<FName>(InputData, Settings->VariantNameSource, VariantNames, Context);
			}
		}

		FPCGResolveMaterialVariantsEntryBundle& Bundle = EntryBundles.Emplace_GetRef();
		Bundle.OutputData = InputData->DuplicateData(Context);
		Bundle.SourceTaggedData = InputTaggedData;
		Bundle.Requests.SetNum(NumEntries);
		Bundle.Keys.SetNum(NumEntries);
		Bundle.bKeyValid.Init(false, NumEntries);
		Bundle.ResolvedMaterials.Init(nullptr, NumEntries);

		if (bIsLocalScope)
		{
			Bundle.ResolvedOwnerComponents.Init(nullptr, NumEntries);
			Bundle.ResolvedCacheComponents.Init(nullptr, NumEntries);
			Bundle.ResolvedOwnerKeys.SetNum(NumEntries);
			Bundle.ResolvedVariantNames.SetNum(NumEntries);
			Bundle.bOwnerValid.Init(false, NumEntries);
		}

		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			++TotalInputEntries;

			FPCGUtilsMaterialVariantRequest& Request = Bundle.Requests[Index];
			Request.ParentMaterial = TSoftObjectPtr<UMaterialInterface>(GetBroadcastValue(ParentPaths, Index, FSoftObjectPath()));

			for (int32 BindingIndex = 0; BindingIndex < Settings->ParameterBindings.Num(); ++BindingIndex)
			{
				const FPCGUtilsMaterialParameterBinding& Binding = Settings->ParameterBindings[BindingIndex];

				FPCGUtilsMaterialParameterOverride Override;
				Override.ParameterInfo = Binding.TargetParameter;
				Override.Type = Binding.Type;

				switch (Binding.Type)
				{
				case EPCGUtilsMaterialParameterType::Scalar:
					Override.ScalarValue = GetBroadcastValue(BindingValues[BindingIndex].Scalars, Index, 0.0f);
					break;
				case EPCGUtilsMaterialParameterType::Vector:
				{
					const FVector4 Value = GetBroadcastValue(BindingValues[BindingIndex].Vectors, Index, FVector4(1.0, 1.0, 1.0, 1.0));
					Override.VectorValue = FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), static_cast<float>(Value.W));
					break;
				}
				case EPCGUtilsMaterialParameterType::Texture:
					Override.TextureValue = TSoftObjectPtr<UTexture>(GetBroadcastValue(BindingValues[BindingIndex].Textures, Index, FSoftObjectPath()));
					break;
				}

				Request.ParameterOverrides.Add(Override);
			}

			FPCGUtilsMaterialVariantKey Key;
			FText CanonicalizeError;
			if (!PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(Request, Key, CanonicalizeError))
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(
					LOCTEXT("EntryCanonicalizeFailed", "Resolve Material Variants: entry {0}: {1}"),
					FText::AsNumber(Index), CanonicalizeError));
				++TotalCanonicalizeFailures;
				continue;
			}

			Bundle.Keys[Index] = Key;
			Bundle.bKeyValid[Index] = true;

			if (!bIsLocalScope)
			{
				UniqueGlobalRequests.FindOrAdd(Key) = Request;
			}
			else
			{
				Bundle.ResolvedVariantNames[Index] = GetBroadcastValue(VariantNames, Index, FName(NAME_None));
			}
		}

		if (bIsLocalScope)
		{
			// Owner/cache-component resolution touches live UObjects - game-thread phase.
			for (int32 Index = 0; Index < NumEntries; ++Index)
			{
				if (!Bundle.bKeyValid[Index])
				{
					continue;
				}

				const FSoftObjectPath OwnerPath = GetBroadcastValue(OwnerPaths, Index, FSoftObjectPath());
				if (OwnerPath.IsNull())
				{
					PCGE_LOG(Warning, GraphAndLog, FText::Format(
						LOCTEXT("EmptyOwnerPath", "Resolve Material Variants: entry {0}: owner component source is empty."),
						FText::AsNumber(Index)));
					continue;
				}

				UActorComponent* OwnerComponent = nullptr;
				FText ResolveError;
				if (!PCGUtilsMaterialCache::TryResolveComponent(OwnerPath, OwnerComponent, &ResolveError))
				{
					PCGE_LOG(Warning, GraphAndLog, FText::Format(
						LOCTEXT("OwnerResolveFailed", "Resolve Material Variants: entry {0}: {1}"),
						FText::AsNumber(Index), ResolveError));
					continue;
				}

				FText CacheLookupError;
				UPCGUtilsLocalMaterialCacheComponent* CacheComponent = PCGUtilsMaterialCache::FindLocalMaterialCacheComponentOnActor(OwnerComponent->GetOwner(), Settings->CacheName, CacheLookupError);
				if (!CacheComponent)
				{
					PCGE_LOG(Warning, GraphAndLog, FText::Format(
						LOCTEXT("CacheComponentLookupFailed", "Resolve Material Variants: entry {0}: {1}"),
						FText::AsNumber(Index), CacheLookupError));
					continue;
				}

				FName OwnerComponentKey;
				if (!PCGUtilsMaterialCache::TryGetComponentOwnerKey(OwnerComponent, OwnerComponentKey))
				{
					PCGE_LOG(Warning, GraphAndLog, FText::Format(
						LOCTEXT("OwnerKeyFailed", "Resolve Material Variants: entry {0}: could not derive an owner key for component '{1}'."),
						FText::AsNumber(Index), FText::FromString(OwnerComponent->GetName())));
					continue;
				}

				Bundle.ResolvedOwnerComponents[Index] = OwnerComponent;
				Bundle.ResolvedCacheComponents[Index] = CacheComponent;
				Bundle.ResolvedOwnerKeys[Index] = OwnerComponentKey;
				Bundle.bOwnerValid[Index] = true;
			}
		}
	}

	// ── Local Component grouping (registry lookup identity: cache component + owner + binding + variant). ──

	TMap<FPCGLocalGroupKey, FPCGLocalGroupData> LocalGroups;
	if (bIsLocalScope)
	{
		for (int32 BundleIndex = 0; BundleIndex < EntryBundles.Num(); ++BundleIndex)
		{
			FPCGResolveMaterialVariantsEntryBundle& Bundle = EntryBundles[BundleIndex];
			for (int32 Index = 0; Index < Bundle.bOwnerValid.Num(); ++Index)
			{
				if (!Bundle.bKeyValid[Index] || !Bundle.bOwnerValid[Index])
				{
					continue;
				}

				FPCGLocalGroupKey GroupKey;
				GroupKey.CacheComponent = Bundle.ResolvedCacheComponents[Index];
				GroupKey.OwnerComponentKey = Bundle.ResolvedOwnerKeys[Index];
				GroupKey.BindingName = Settings->BindingName;
				GroupKey.VariantName = Bundle.ResolvedVariantNames[Index];

				FPCGLocalGroupData& GroupData = LocalGroups.FindOrAdd(GroupKey);
				if (!GroupData.bHasFirstKey)
				{
					GroupData.FirstInitializationKey = Bundle.Keys[Index];
					GroupData.bHasFirstKey = true;
					GroupData.RepresentativeBundleIndex = BundleIndex;
					GroupData.RepresentativeEntryIndex = Index;
				}
				else if (!(GroupData.FirstInitializationKey == Bundle.Keys[Index]))
				{
					GroupData.bConflict = true;
				}

				GroupData.EntryLocations.Emplace(BundleIndex, Index);
			}
		}
	}

	const int32 NumUniqueRequestsOrGroups = bIsLocalScope ? LocalGroups.Num() : UniqueGlobalRequests.Num();

	if (Settings->bEnableMaximumVariants && NumUniqueRequestsOrGroups > Settings->MaximumVariants)
	{
		PCGE_LOG(Error, GraphAndLog, FText::Format(
			LOCTEXT("MaxVariantsExceeded", "Resolve Material Variants: {0} unique material variants requested, exceeding the maximum of {1}. Failing rather than generating an uncontrolled number of variants."),
			FText::AsNumber(NumUniqueRequestsOrGroups), FText::AsNumber(Settings->MaximumVariants)));
		return true;
	}

	if (NumUniqueRequestsOrGroups > Settings->VariantWarningThreshold)
	{
		PCGE_LOG(Warning, GraphAndLog, FText::Format(
			LOCTEXT("VariantThresholdExceeded", "Resolve Material Variants: {0} unique material variants requested (threshold {1}). Excessive material variants can increase dynamic material instance count, ISM material grouping, and rendering/memory cost."),
			FText::AsNumber(NumUniqueRequestsOrGroups), FText::AsNumber(Settings->VariantWarningThreshold)));
	}

	// ── Phase B (game-thread): resolve each unique request/group exactly once. ──

	int32 NumFailedResolutions = 0;
	int32 NumConflicts = 0;
	int32 CacheHitsDelta = 0;
	int32 CacheMissesDelta = 0;

	if (!bIsLocalScope)
	{
		const FPCGUtilsMaterialVariantCacheStatistics StatsBefore = GlobalSubsystem->GetStatistics();

		TMap<FPCGUtilsMaterialVariantKey, FPCGUtilsMaterialVariantResolveResult> ResolvedByKey;
		ResolvedByKey.Reserve(NumUniqueRequestsOrGroups);

		for (const TPair<FPCGUtilsMaterialVariantKey, FPCGUtilsMaterialVariantRequest>& Pair : UniqueGlobalRequests)
		{
			FPCGUtilsMaterialVariantResolveResult Result = GlobalSubsystem->ResolveMaterialVariant(Pair.Value);
			if (!Result.bSucceeded)
			{
				PCGE_LOG(Warning, GraphAndLog, Result.ErrorMessage);
				++NumFailedResolutions;
			}
			ResolvedByKey.Add(Pair.Key, MoveTemp(Result));
		}

		const FPCGUtilsMaterialVariantCacheStatistics StatsAfter = GlobalSubsystem->GetStatistics();
		CacheHitsDelta = StatsAfter.CacheHits - StatsBefore.CacheHits;
		CacheMissesDelta = StatsAfter.CacheMisses - StatsBefore.CacheMisses;

		for (FPCGResolveMaterialVariantsEntryBundle& Bundle : EntryBundles)
		{
			for (int32 Index = 0; Index < Bundle.Keys.Num(); ++Index)
			{
				if (!Bundle.bKeyValid[Index])
				{
					continue;
				}
				if (const FPCGUtilsMaterialVariantResolveResult* Result = ResolvedByKey.Find(Bundle.Keys[Index]))
				{
					if (Result->bSucceeded)
					{
						Bundle.ResolvedMaterials[Index] = Result->Material;
					}
				}
			}
		}
	}
	else
	{
		for (TPair<FPCGLocalGroupKey, FPCGLocalGroupData>& GroupPair : LocalGroups)
		{
			const FPCGLocalGroupKey& GroupKey = GroupPair.Key;
			FPCGLocalGroupData& GroupData = GroupPair.Value;

			if (GroupData.bConflict)
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(
					LOCTEXT("ConflictingBinding", "Resolve Material Variants: multiple material configurations attempted to claim the same local material binding (owner '{0}', binding '{1}', variant '{2}'). Use distinct Variant Names or ensure all points owned by this component use the same initialization configuration."),
					FText::FromName(GroupKey.OwnerComponentKey), FText::FromName(GroupKey.BindingName), FText::FromName(GroupKey.VariantName)));
				++NumConflicts;
				continue;
			}

			UActorComponent* OwnerComponent = EntryBundles[GroupData.RepresentativeBundleIndex].ResolvedOwnerComponents[GroupData.RepresentativeEntryIndex];
			const FPCGUtilsMaterialVariantRequest& Request = EntryBundles[GroupData.RepresentativeBundleIndex].Requests[GroupData.RepresentativeEntryIndex];

			UMaterialInstanceDynamic* Resolved = GroupKey.CacheComponent->ResolveLocalMaterial(OwnerComponent, GroupKey.BindingName, GroupKey.VariantName, Request);
			if (!Resolved)
			{
				++NumFailedResolutions;
				continue;
			}

			GroupData.ResolvedMaterial = Resolved;
			GroupData.bResolved = true;
		}

		for (const TPair<FPCGLocalGroupKey, FPCGLocalGroupData>& GroupPair : LocalGroups)
		{
			const FPCGLocalGroupData& GroupData = GroupPair.Value;
			if (!GroupData.bResolved)
			{
				continue;
			}

			for (const TPair<int32, int32>& Location : GroupData.EntryLocations)
			{
				EntryBundles[Location.Key].ResolvedMaterials[Location.Value] = GroupData.ResolvedMaterial;
			}
		}
	}

	// ── Assign outputs (one write per input entry, uniform across both cache scopes). ──

	int32 NumOutputMaterialsAssigned = 0;
	int32 NumUnrepresentableLiveMIDs = 0;

	for (FPCGResolveMaterialVariantsEntryBundle& Bundle : EntryBundles)
	{
		const int32 NumEntries = Bundle.Keys.Num();
		TArray<FSoftObjectPath> OutputPaths;
		OutputPaths.SetNum(NumEntries);

		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			UMaterialInterface* Resolved = Bundle.ResolvedMaterials[Index];
			if (!Resolved)
			{
				OutputPaths[Index] = FSoftObjectPath();
				continue;
			}

			if (Resolved->IsA<UMaterialInstanceDynamic>())
			{
				// A transient MID has no stable asset path - do not fake one. Documented V1
				// limitation: consume this variant via the material cache C++/Blueprint API
				// with an equivalent request instead (for Local Component mode, this is in
				// fact the intended primary retrieval path, not a fallback).
				OutputPaths[Index] = FSoftObjectPath();
				++NumUnrepresentableLiveMIDs;
				continue;
			}

			OutputPaths[Index] = FSoftObjectPath(Resolved);
			++NumOutputMaterialsAssigned;
		}

		if (!PCGAttributeAccessorHelpers::WriteAllValues<FSoftObjectPath>(Bundle.OutputData, Settings->OutputMaterialAttribute, OutputPaths, &Settings->ParentMaterialSource, Context))
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("OutputWriteFailed", "Resolve Material Variants: failed to write the resolved material output attribute for one input."));
			continue;
		}

		FPCGTaggedData& OutputTaggedData = Context->OutputData.TaggedData.Add_GetRef(Bundle.SourceTaggedData);
		OutputTaggedData.Data = Bundle.OutputData;
	}

	if (NumUnrepresentableLiveMIDs > 0)
	{
		PCGE_LOG(Warning, GraphAndLog, FText::Format(
			LOCTEXT("UnrepresentableMIDs", "Resolve Material Variants: {0} entrie(s) resolved to a dynamically-created material instance, which cannot be represented as a static asset reference and were left unset on the output attribute."),
			FText::AsNumber(NumUnrepresentableLiveMIDs)));
	}

	if (bIsLocalScope)
	{
		PCGE_LOG(Verbose, GraphAndLog, FText::Format(
			LOCTEXT("LocalDiagnostics", "Resolve Material Variants (Local): {0} input entries, {1} unique bindings ({2} local duplicates removed), {3} conflicting bindings rejected, {4} failed resolutions, {5} materials assigned to output."),
			FText::AsNumber(TotalInputEntries),
			FText::AsNumber(NumUniqueRequestsOrGroups),
			FText::AsNumber(TotalInputEntries - TotalCanonicalizeFailures - NumUniqueRequestsOrGroups),
			FText::AsNumber(NumConflicts),
			FText::AsNumber(NumFailedResolutions),
			FText::AsNumber(NumOutputMaterialsAssigned)));
	}
	else
	{
		PCGE_LOG(Verbose, GraphAndLog, FText::Format(
			LOCTEXT("GlobalDiagnostics", "Resolve Material Variants (Global): {0} input entries, {1} unique requests ({2} local duplicates removed), {3} cache hits, {4} cache misses, {5} failed requests, {6} materials assigned to output."),
			FText::AsNumber(TotalInputEntries),
			FText::AsNumber(NumUniqueRequestsOrGroups),
			FText::AsNumber(TotalInputEntries - TotalCanonicalizeFailures - NumUniqueRequestsOrGroups),
			FText::AsNumber(CacheHitsDelta),
			FText::AsNumber(CacheMissesDelta),
			FText::AsNumber(NumFailedResolutions),
			FText::AsNumber(NumOutputMaterialsAssigned)));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
