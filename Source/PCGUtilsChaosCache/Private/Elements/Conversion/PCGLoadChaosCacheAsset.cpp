// Copyright Max Harris

#include "Elements/Conversion/PCGLoadChaosCacheAsset.h"

#include "Chaos/CacheCollection.h"
#include "Chaos/ChaosCache.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PCGPin.h"
#include "PCGUtilsChaosCache.h"
#include "Sampling/PCGUtilsChaosCacheSampling.h"
#include "Utils/PCGLogErrors.h"

#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "PCGLoadChaosCacheAsset"

namespace PCGLoadChaosCacheAsset
{
	FText CacheText(const UChaosCache& Cache)
	{
		return FText::FromName(Cache.GetFName());
	}

	FText SecondsText(float Seconds)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 3;
		return FText::AsNumber(Seconds, &Options);
	}

	/**
	 * Resolves the sample time against one cache's recorded range, warning when it had to clamp. The range is
	 * per cache, because the caches in one collection can have recorded for different lengths.
	 */
	float ResolveSampleTime(
		const UPCGLoadChaosCacheAssetSettings& Settings, const UChaosCache& Cache, float EndTime, FPCGContext* Context)
	{
		if (Settings.bNormalizedTime)
		{
			const float Fraction = FMath::Clamp(Settings.SampleTime, 0.0f, 1.0f);
			if (Fraction != Settings.SampleTime)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NormalizedTimeOutOfRange",
						"Sample Time {0} is outside the normalized range 0 to 1 (Normalized Time is on). Cache '{1}' "
						"was sampled at {2} instead ({3}s of its {4}s recording)."),
					FText::AsNumber(Settings.SampleTime), CacheText(Cache), FText::AsNumber(Fraction),
					SecondsText(Fraction * EndTime), SecondsText(EndTime)), Context);
			}
			return Fraction * EndTime;
		}

		const float Time = FMath::Clamp(Settings.SampleTime, 0.0f, EndTime);
		if (Time != Settings.SampleTime)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("AbsoluteTimeOutOfRange",
					"Sample Time {0}s is outside the recorded range of cache '{1}', 0 to {2}s. It was sampled at "
					"{3}s instead."),
				SecondsText(Settings.SampleTime), CacheText(Cache), SecondsText(EndTime), SecondsText(Time)), Context);
		}
		return Time;
	}

	FTransform ResolvePlacement(const UPCGLoadChaosCacheAssetSettings& Settings, const UChaosCache& Cache)
	{
		// Rigid parts only, matching how the recorded transforms are brought into collection space.
		const FCacheSpawnableTemplate& Template = Cache.GetSpawnableTemplate();
		switch (Settings.Space)
		{
		case EPCGChaosCacheSampleSpace::CacheManager:
			return PCGUtilsChaosCacheSampling::RigidPart(Template.ComponentTransform);
		case EPCGChaosCacheSampleSpace::RecordedWorld:
			return PCGUtilsChaosCacheSampling::RigidPart(Template.InitialTransform);
		case EPCGChaosCacheSampleSpace::Component:
		default:
			return FTransform::Identity;
		}
	}

	/**
	 * Samples one cache and emits its GC data. Every failure is reported here, naming the cache, because a
	 * collection can hold several and the user has to know which one to look at.
	 *
	 * @param bNamedExplicitly  The user asked for this cache by name, so skipping it is an error rather than a
	 *                          warning.
	 */
	void SampleCache(
		FPCGContext* Context,
		const UPCGLoadChaosCacheAssetSettings& Settings,
		const UChaosCacheCollection& CacheCollection,
		const UChaosCache& Cache,
		const UGeometryCollection* RestCollectionOverride,
		bool bNamedExplicitly)
	{
		using namespace PCGUtilsChaosCacheSampling;

		const FText CollectionName = FText::FromString(CacheCollection.GetName());
		const UObject* RecordedTemplate = Cache.GetSpawnableTemplate().DuplicatedTemplate;
		const UGeometryCollectionComponent* RecordedComponent = GetRecordedGeometryCollectionComponent(Cache);
		const bool bRecorded = HasRecording(Cache);

		// A Cache Manager can observe static meshes too; those caches say nothing about a Geometry Collection.
		if (RecordedTemplate && !RecordedComponent)
		{
			const FText Message = FText::Format(
				LOCTEXT("NotGeometryCollectionCache",
					"Cache '{0}' in '{1}' recorded a {2}, not a Geometry Collection component, so it cannot be "
					"sampled as GC data."),
				CacheText(Cache), CollectionName, FText::FromString(RecordedTemplate->GetClass()->GetName()));
			bNamedExplicitly ? PCGLog::LogErrorOnGraph(Message, Context) : PCGLog::LogWarningOnGraph(Message, Context);
			return;
		}

		const UGeometryCollection* RestAsset = RestCollectionOverride;
		if (!RestAsset && RecordedComponent)
		{
			RestAsset = RecordedComponent->GetRestCollection();
		}

		if (!bRecorded)
		{
			PCGLog::LogWarningOnGraph(RestAsset
				? FText::Format(LOCTEXT("NotRecordedRestState",
					"Cache '{0}' in '{1}' has not been recorded: it holds no transform tracks. Emitting the "
					"unsimulated rest state of '{2}'."),
					CacheText(Cache), CollectionName, FText::FromString(RestAsset->GetName()))
				: FText::Format(LOCTEXT("NotRecordedNoGeometry",
					"Cache '{0}' in '{1}' has not been recorded: it holds no transform tracks and no recorded "
					"component, so there is no geometry to emit. Record it with a Chaos Cache Manager, or set "
					"Rest Collection Override to emit the unsimulated rest state."),
					CacheText(Cache), CollectionName), Context);
			if (!RestAsset)
			{
				return;
			}
		}
		else if (!RestAsset)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NoRestCollection",
					"Cache '{0}' in '{1}' recorded a Geometry Collection component whose rest collection was not "
					"saved with the cache (it was probably transient). Set Rest Collection Override to the asset "
					"it was recorded from."),
				CacheText(Cache), CollectionName), Context);
			return;
		}

		const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> SourceCollection = RestAsset->GetGeometryCollection();
		const int32 NumBones = SourceCollection.IsValid()
			? SourceCollection->NumElements(FGeometryCollection::TransformGroup) : 0;
		if (NumBones == 0)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("EmptyRestCollection",
					"Rest collection '{0}' of cache '{1}' holds no bones, so there is nothing to sample."),
				FText::FromString(RestAsset->GetName()), CacheText(Cache)), Context);
			return;
		}

		// Track particle indices are bone indices of the collection the cache was recorded against, so every one
		// must exist in the collection being posed - the engine's own playback check.
		int32 InvalidTrack = INDEX_NONE;
		int32 InvalidParticle = INDEX_NONE;
		if (FindInvalidTrack(Cache, NumBones, InvalidTrack, InvalidParticle))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("TrackOutOfRange",
					"Cache '{0}' track {1} records bone {2}, but rest collection '{3}' has {4} bones (valid bone "
					"indices are 0 to {5}). The cache was recorded against a different collection."),
				CacheText(Cache), FText::AsNumber(InvalidTrack), FText::AsNumber(InvalidParticle),
				FText::FromString(RestAsset->GetName()), FText::AsNumber(NumBones), FText::AsNumber(NumBones - 1)),
				Context);
			return;
		}

		// Deep copy: the asset keeps its own collection. Same reasoning as GC | From Asset.
		TSharedRef<FGeometryCollection> Collection = MakeShared<FGeometryCollection>();
		SourceCollection->CopyTo(&Collection.Get());

		TArray<FString> MissingAttributes;
		if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(*Collection, MissingAttributes))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MalformedRestCollection",
					"Rest collection '{0}' of cache '{1}' is missing the attribute(s) GC data requires: {2}."),
				FText::FromString(RestAsset->GetName()), CacheText(Cache),
				FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), Context);
			return;
		}

		float SampledTime = 0.0f;
		if (bRecorded)
		{
			const FTransform& ComponentToCacheManager = Cache.GetSpawnableTemplate().ComponentTransform;
			if (!ComponentToCacheManager.GetScale3D().Equals(FVector::OneVector))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("ScaledComponent",
						"Cache '{0}' recorded a component scaled by {1}. Chaos records unscaled rigid transforms for "
						"scaled geometry, which GC data cannot represent exactly, so the scale is ignored and piece "
						"positions will be off. Record with a unit-scale component."),
					CacheText(Cache), FText::FromString(ComponentToCacheManager.GetScale3D().ToString())), Context);
			}

			SampledTime = ResolveSampleTime(Settings, Cache, ComputeEndTime(Cache), Context);

			// Applied before publishing, while bone indices still match the rest collection the cache was
			// recorded against.
			const FSampleResult Result = ApplyCacheState(Cache, SampledTime, ComponentToCacheManager, *Collection);
			if (Result.Status == ESampleStatus::RecordingInProgress)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("RecordingInProgress",
						"Cache '{0}' in '{1}' is open for recording and cannot be sampled until the recording ends."),
					CacheText(Cache), CollectionName), Context);
				return;
			}

			if (!Result.TransformResult.UnrepresentableBones.IsEmpty())
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("UnrepresentableBones",
						"Cache '{0}' supplied {1} non-finite or unrepresentable bone transform(s) at {2}s (first: "
						"bone {3}); those bones were left at their rest transform."),
					CacheText(Cache), FText::AsNumber(Result.TransformResult.UnrepresentableBones.Num()),
					SecondsText(SampledTime), FText::AsNumber(Result.TransformResult.UnrepresentableBones[0])),
					Context);
			}

			UE_LOG(LogPCGUtilsChaosCache, Verbose, TEXT("GC Load Chaos Cache: '%s' at %.3fs - %d bone(s) sampled, %d released cluster(s)."),
				*Cache.GetName(), SampledTime, Result.NumSampledBones, Result.NumReleasedClusters);
		}

		const FTransform Placement = ResolvePlacement(Settings, Cache);
		if (!Placement.Equals(FTransform::Identity))
		{
			PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Collection, Placement);
		}

		TArray<TObjectPtr<UMaterialInterface>> Materials;
		if (Settings.bExtractMaterials)
		{
			Materials = RestAsset->Materials;
		}

		// A new lineage: nothing upstream produced this collection. Publishing also supplies the Level
		// attribute, bone ids and material sections downstream selectors rely on.
		FPCGUtilsGeometryCollectionPublishOptions PublishOptions;
		PublishOptions.bCompactHiddenGeometry = !Settings.bKeepHiddenGeometry;

		UPCGGeometryCollectionData* OutputData = PCGUtilsGeometryCollectionRevisionPublisher::PublishNewLineage(
			Context, Collection, MoveTemp(Materials), PublishOptions);
		if (!OutputData)
		{
			return;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGLoadChaosCacheAssetConstants::CollectionOutputPin;
		Output.Tags.Add(Cache.GetName());

		UE_LOG(LogPCGUtilsChaosCache, Verbose, TEXT("GC Load Chaos Cache: '%s' -> %s"),
			*Cache.GetName(), *PCGUtilsGeometryCollectionHelpers::DescribeCollection(*Collection));
	}
}

#if WITH_EDITOR
FText UPCGLoadChaosCacheAssetSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Load Chaos Cache");
}

FText UPCGLoadChaosCacheAssetSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Samples a Chaos Cache recording of a Geometry Collection at one moment and emits that bone state as GC "
		"data. Record the simulation outside PCG with a Chaos Cache Manager, then pick the time here - in seconds, "
		"or normalized over the recording. The cache and the Geometry Collection asset are only read; the "
		"geometry is the recorded component's rest collection, posed as the simulation had it.");
}

FString UPCGLoadChaosCacheAssetSettings::GetAdditionalTitleInformation() const
{
	// The one thing someone reads the graph to check is when it samples.
	if (bNormalizedTime)
	{
		return FText::AsPercent(SampleTime).ToString();
	}
	return FString::Printf(TEXT("%ss"), *PCGLoadChaosCacheAsset::SecondsText(SampleTime).ToString());
}

void UPCGLoadChaosCacheAssetSettings::GetStaticTrackedKeys(
	FPCGSelectionKeyToSettingsMap& OutKeysToSettings,
	TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	// Tracking the cache is what refreshes the graph when the simulation is recorded again. Overridden assets are
	// not static - they are tracked per execution in ExecuteInternal instead.
	if (!CacheCollection.IsNull() &&
		!IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGLoadChaosCacheAssetSettings, CacheCollection)))
	{
		OutKeysToSettings.FindOrAdd(FPCGSelectionKey::CreateFromPath(CacheCollection.ToSoftObjectPath()))
			.Emplace(this, /*bCulling=*/false);
	}

	if (!RestCollectionOverride.IsNull() &&
		!IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGLoadChaosCacheAssetSettings, RestCollectionOverride)))
	{
		OutKeysToSettings.FindOrAdd(FPCGSelectionKey::CreateFromPath(RestCollectionOverride.ToSoftObjectPath()))
			.Emplace(this, /*bCulling=*/false);
	}
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGLoadChaosCacheAssetSettings::InputPinProperties() const
{
	// A source element: it has nothing to operate on, only assets to read.
	return {};
}

TArray<FPCGPinProperties> UPCGLoadChaosCacheAssetSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGLoadChaosCacheAssetConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true));
	return Pins;
}

FPCGElementPtr UPCGLoadChaosCacheAssetSettings::CreateElement() const
{
	return MakeShared<FPCGLoadChaosCacheAssetElement>();
}

bool FPCGLoadChaosCacheAssetElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Without a context we cannot tell which phase this is, so assume the one that needs the game thread.
	return !Context || Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
}

FPCGContext* FPCGLoadChaosCacheAssetElement::CreateContext()
{
	return new FPCGLoadChaosCacheAssetContext();
}

bool FPCGLoadChaosCacheAssetElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLoadChaosCacheAssetElement::PrepareData);

	FPCGLoadChaosCacheAssetContext* Context = static_cast<FPCGLoadChaosCacheAssetContext*>(InContext);
	check(Context);

	const UPCGLoadChaosCacheAssetSettings* Settings = Context->GetInputSettings<UPCGLoadChaosCacheAssetSettings>();
	check(Settings);

	if (Context->WasLoadRequested() || Settings->CacheCollection.IsNull())
	{
		return true;
	}

	TArray<FSoftObjectPath> Paths = {Settings->CacheCollection.ToSoftObjectPath()};
	if (!Settings->RestCollectionOverride.IsNull())
	{
		Paths.Add(Settings->RestCollectionOverride.ToSoftObjectPath());
	}

	// Asynchronous by default, so a cold asset does not stall the graph's whole execution.
	return Context->RequestResourceLoad(Context, MoveTemp(Paths), !Settings->bSynchronousLoad);
}

bool FPCGLoadChaosCacheAssetElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLoadChaosCacheAssetElement::Execute);
	check(Context);

	const UPCGLoadChaosCacheAssetSettings* Settings = Context->GetInputSettings<UPCGLoadChaosCacheAssetSettings>();
	check(Settings);

	if (Settings->CacheCollection.IsNull())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoAsset", "GC | Load Chaos Cache has no Chaos Cache Collection asset selected."), Context);
		return true;
	}

	const UChaosCacheCollection* Collection = Settings->CacheCollection.Get();
	if (!Collection)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("AssetFailedToLoad", "Chaos Cache Collection asset '{0}' failed to load."),
			FText::FromString(Settings->CacheCollection.ToSoftObjectPath().ToString())), Context);
		return true;
	}

	const UGeometryCollection* RestCollectionOverride = nullptr;
	if (!Settings->RestCollectionOverride.IsNull())
	{
		RestCollectionOverride = Settings->RestCollectionOverride.Get();
		if (!RestCollectionOverride)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("OverrideFailedToLoad", "Rest Collection Override asset '{0}' failed to load."),
				FText::FromString(Settings->RestCollectionOverride.ToSoftObjectPath().ToString())), Context);
			return true;
		}
	}

#if WITH_EDITOR
	if (Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGLoadChaosCacheAssetSettings, CacheCollection)))
	{
		FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(
			Context, FPCGSelectionKey::CreateFromPath(Settings->CacheCollection.ToSoftObjectPath()), /*bIsCulled=*/false);
	}
	if (RestCollectionOverride &&
		Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGLoadChaosCacheAssetSettings, RestCollectionOverride)))
	{
		FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(
			Context, FPCGSelectionKey::CreateFromPath(Settings->RestCollectionOverride.ToSoftObjectPath()),
			/*bIsCulled=*/false);
	}
#endif

	const FText CollectionName = FText::FromString(Collection->GetName());

	if (Settings->CacheName != NAME_None)
	{
		const UChaosCache* Cache = Collection->FindCache(Settings->CacheName);
		if (!Cache)
		{
			TArray<FString> Names;
			for (const UChaosCache* Candidate : Collection->GetCaches())
			{
				if (Candidate)
				{
					Names.Add(Candidate->GetName());
				}
			}
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("CacheNotFound", "Chaos Cache Collection '{0}' has no cache named '{1}'. It holds: {2}."),
				CollectionName, FText::FromName(Settings->CacheName),
				Names.IsEmpty() ? LOCTEXT("NoCaches", "no caches") : FText::FromString(FString::Join(Names, TEXT(", ")))),
				Context);
			return true;
		}

		PCGLoadChaosCacheAsset::SampleCache(Context, *Settings, *Collection, *Cache, RestCollectionOverride,
			/*bNamedExplicitly=*/true);
		return true;
	}

	int32 NumCaches = 0;
	for (const UChaosCache* Cache : Collection->GetCaches())
	{
		if (Cache)
		{
			++NumCaches;
			PCGLoadChaosCacheAsset::SampleCache(Context, *Settings, *Collection, *Cache, RestCollectionOverride,
				/*bNamedExplicitly=*/false);
		}
	}

	if (NumCaches == 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("EmptyCollection",
				"Chaos Cache Collection '{0}' holds no caches: it has not been recorded. Record it with a Chaos "
				"Cache Manager observing a Geometry Collection component."),
			CollectionName), Context);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
