// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"

class FGeometryCollection;
class UChaosCache;
class UGeometryCollectionComponent;

/**
 * Reading a Geometry Collection's bone state out of a recorded UChaosCache.
 *
 * What the cache holds, briefly (Docs/PCGUtilsChaosCache.md has the engine citations): one transform track per
 * recorded particle, where the GC adapter's particle index *is* the bone index of the rest collection, each key
 * being the bone's world transform expressed relative to the Chaos Cache Manager that recorded it. Only active
 * particles are recorded - the children of an intact cluster are not, and ride along with it - and a cluster
 * that releases gets a final key and is flagged `bDeactivateOnEnd`.
 *
 * Only UChaosCache's exported API and its public UPROPERTY fields are used here. The track and event types in
 * ChaosCaching's headers (FParticleTransformTrack's methods, FCacheEventTrack, FEnableStateEvent) are not
 * exported and fail to link from another module.
 */
namespace PCGUtilsChaosCacheSampling
{
	/** The recorded Geometry Collection component template, or null if the cache recorded something else or nothing. */
	PCGUTILSCHAOSCACHE_API const UGeometryCollectionComponent* GetRecordedGeometryCollectionComponent(const UChaosCache& Cache);

	/** True when the cache has at least one keyed transform track, i.e. a recording actually happened. */
	PCGUTILSCHAOSCACHE_API bool HasRecording(const UChaosCache& Cache);

	/**
	 * The latest key time across every track, in cache seconds. The sampleable range is [0, EndTime].
	 *
	 * Not UChaosCache::GetDuration(): that is `lastKey - firstKey`, and the first key sits one physics step after
	 * zero, so treating the duration as the end would leave "the end of the recording" just short of the final
	 * recorded state.
	 */
	PCGUTILSCHAOSCACHE_API float ComputeEndTime(const UChaosCache& Cache);

	/**
	 * Finds the first track whose particle index is not a bone of a collection with InNumBones bones - the check
	 * the engine's own FGeometryCollectionCacheAdapter::ValidForPlayback makes.
	 *
	 * @return false if every track maps to a valid bone.
	 */
	PCGUTILSCHAOSCACHE_API bool FindInvalidTrack(
		const UChaosCache& Cache, int32 InNumBones, int32& OutTrackIndex, int32& OutParticleIndex);

	enum class ESampleStatus : uint8
	{
		Success,
		/** BeginPlayback refused: the cache is open for recording. */
		RecordingInProgress,
	};

	struct FSampleResult
	{
		ESampleStatus Status = ESampleStatus::Success;

		/** Bones the cache supplied a transform for at the sampled time, including released clusters. */
		int32 NumSampledBones = 0;

		/** Clusters that had released before the sampled time and are held at their release pose. */
		int32 NumReleasedClusters = 0;

		/** What the bone-transform write reported - invalid or unrepresentable bones are listed here. */
		FPCGUtilsGeometryCollectionBoneTransformResult TransformResult;
	};

	/**
	 * Poses InOutCollection as the cache recorded it at InTime.
	 *
	 * InOutCollection must be (a copy of) the rest collection the cache was recorded against, in its own local
	 * space, with every track already validated by FindInvalidTrack. Bones the cache has no active track for at
	 * InTime keep their stored local transform, so they follow their parent - which is what Chaos does for the
	 * children of an intact cluster, and what the engine does before a track has begun.
	 *
	 * Deliberately *not* a copy of FGeometryCollectionCacheAdapter::SetRestState. That writes component-space
	 * transforms straight into the bones' parent-relative slots and zeroes every released cluster so the
	 * renderer comes out right; the result is not a consistent hierarchy. Here every sampled transform is
	 * written as a collection-space transform through PCGUtilsGeometryCollectionTransforms, nearest the root
	 * first, and a released cluster keeps its release pose rather than identity.
	 *
	 * @param InComponentToCacheManager  The recorded component relative to the Cache Manager - the spawnable
	 *                                   template's ComponentTransform. Only its rotation and translation are
	 *                                   used; see the scale note in Docs/PCGUtilsChaosCache.md.
	 */
	PCGUTILSCHAOSCACHE_API FSampleResult ApplyCacheState(
		const UChaosCache& Cache,
		float InTime,
		const FTransform& InComponentToCacheManager,
		FGeometryCollection& InOutCollection);

	/** InTransform with its scale dropped - the rigid part the recording can represent. */
	inline FTransform RigidPart(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetTranslation());
	}
}
