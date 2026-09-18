// Copyright Max Harris

#include "Sampling/PCGUtilsChaosCacheSampling.h"

#include "Chaos/ChaosCache.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"

namespace PCGUtilsChaosCacheSampling
{
	const UGeometryCollectionComponent* GetRecordedGeometryCollectionComponent(const UChaosCache& Cache)
	{
		return Cast<UGeometryCollectionComponent>(Cache.GetSpawnableTemplate().DuplicatedTemplate);
	}

	bool HasRecording(const UChaosCache& Cache)
	{
		for (const FPerParticleCacheData& Track : Cache.ParticleTracks)
		{
			if (!Track.TransformData.KeyTimestamps.IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	float ComputeEndTime(const UChaosCache& Cache)
	{
		// Read from the key arrays directly: FParticleTransformTrack::GetEndTime is not exported.
		float EndTime = 0.0f;
		for (const FPerParticleCacheData& Track : Cache.ParticleTracks)
		{
			if (!Track.TransformData.KeyTimestamps.IsEmpty())
			{
				EndTime = FMath::Max(EndTime, Track.TransformData.KeyTimestamps.Last());
			}
		}
		return EndTime;
	}

	bool FindInvalidTrack(const UChaosCache& Cache, int32 InNumBones, int32& OutTrackIndex, int32& OutParticleIndex)
	{
		for (int32 TrackIndex = 0; TrackIndex < Cache.TrackToParticle.Num(); ++TrackIndex)
		{
			const int32 ParticleIndex = Cache.TrackToParticle[TrackIndex];
			if (ParticleIndex < 0 || ParticleIndex >= InNumBones)
			{
				OutTrackIndex = TrackIndex;
				OutParticleIndex = ParticleIndex;
				return true;
			}
		}
		return false;
	}

	FSampleResult ApplyCacheState(
		const UChaosCache& Cache,
		float InTime,
		const FTransform& InComponentToCacheManager,
		FGeometryCollection& InOutCollection)
	{
		FSampleResult Result;

		// Cache Manager space -> the rest collection's own local space.
		const FTransform CacheManagerToCollection = RigidPart(InComponentToCacheManager).Inverse();

		// Collected per bone so a bone can only be written once, whichever route supplied it.
		TMap<int32, FTransform> SampledBones;

		{
			// Evaluate returns an empty result (with only a log warning) unless a playback session is open, and
			// refuses one while the cache is being recorded.
			FCacheUserToken Token = Cache.BeginPlayback();
			if (!Token.IsOpen())
			{
				Result.Status = ESampleStatus::RecordingInProgress;
				return Result;
			}

			// A fresh tick record per random-access sample: reusing one carries LastTime forward.
			FPlaybackTickRecord TickRecord;
			TickRecord.SetLastTime(InTime);

			FCacheEvaluationContext Context(TickRecord);
			Context.bEvaluateTransform = true;
			Context.bEvaluateCurves = false;
			Context.bEvaluateEvents = false;
			Context.bEvaluateChannels = false; // Not initialised by the context's constructor.
			Context.bEvaluateNamedTransforms = false;

			// No MassToLocal: the recorded keys are already bone transforms, which is what the collection stores.
			const FCacheEvaluationResult Evaluated = Cache.Evaluate(Context, nullptr);
			Cache.EndPlayback(Token);

			// The result is compacted - tracks that have not begun or have deactivated are skipped - so it is read
			// through ParticleIndices, never by position.
			for (int32 Index = 0; Index < Evaluated.ParticleIndices.Num() && Index < Evaluated.Transform.Num(); ++Index)
			{
				const int32 Bone = Evaluated.ParticleIndices[Index];
				if (Bone != INDEX_NONE)
				{
					SampledBones.Add(Bone, Evaluated.Transform[Index] * CacheManagerToCollection);
				}
			}
		}

		// A cluster that released before InTime was skipped above. It still exists as a bone here, so hold it at
		// its release pose - its track's final key - rather than leaving it at rest, far from its own pieces. This
		// is the same test Evaluate uses to skip it, read from the track fields because FParticleTransformTrack's
		// methods are not exported.
		for (int32 TrackIndex = 0; TrackIndex < Cache.ParticleTracks.Num(); ++TrackIndex)
		{
			const FPerParticleCacheData& Track = Cache.ParticleTracks[TrackIndex];
			const TArray<float>& Keys = Track.TransformData.KeyTimestamps;
			if (!Track.TransformData.bDeactivateOnEnd || Keys.IsEmpty() || Keys.Last() >= InTime ||
				!Cache.TrackToParticle.IsValidIndex(TrackIndex))
			{
				continue;
			}

			FTransform ReleasePose;
			Cache.EvaluateTransform(Track, Keys.Last(), nullptr, ReleasePose);
			SampledBones.Add(Cache.TrackToParticle[TrackIndex], ReleasePose * CacheManagerToCollection);
			++Result.NumReleasedClusters;
		}

		Result.NumSampledBones = SampledBones.Num();
		if (SampledBones.IsEmpty())
		{
			return Result;
		}

		TArray<int32> Bones;
		TArray<FTransform> Transforms;
		Bones.Reserve(SampledBones.Num());
		Transforms.Reserve(SampledBones.Num());
		for (const TPair<int32, FTransform>& Pair : SampledBones)
		{
			Bones.Add(Pair.Key);
			Transforms.Add(Pair.Value);
		}

		// Independent: every sampled bone reaches exactly its recorded transform, whatever its ancestors did.
		// A released cluster and the pieces it released are both sampled, and each must land where it was.
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
			InOutCollection, Bones, Transforms, EPCGGeometryCollectionNestedBoneHandling::Independent,
			Result.TransformResult);

		return Result;
	}
}
