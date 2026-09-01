// Copyright Max Harris

#include "Spike/PCGSimulationSpikeActor.h"

#include "Chaos/CacheCollection.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/PCGSimulationCacheAdapter.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PCGSimulationBodiesComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PCGUtilsSimulationModule.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSimulationSpikeActor)

namespace PCGSimulationSpikeActorLocal
{
	/** Component names must be deterministic: FObservedComponent resolves by path-from-owner. */
	static const FName BodiesComponentName(TEXT("PCGSim_Bodies"));
	static const FName VisualizationComponentName(TEXT("PCGSim_Visualization"));
	static const FName FloorComponentName(TEXT("PCGSim_Floor"));

	static FString FormatTransform(const FTransform& T)
	{
		return FString::Printf(TEXT("P=(%.1f, %.1f, %.1f) R=(%.1f, %.1f, %.1f)"),
			T.GetTranslation().X, T.GetTranslation().Y, T.GetTranslation().Z,
			T.Rotator().Pitch, T.Rotator().Yaw, T.Rotator().Roll);
	}

	static float FindCurve(const TMap<FName, float>& Curves, FName Name)
	{
		const float* Found = Curves.Find(Name);
		return Found ? *Found : 0.0f;
	}
}

APCGSimulationSpikeActor::APCGSimulationSpikeActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// Recording is the entire point of this actor, and it must not start until the runtime
	// representation exists.
	CacheMode = ECacheMode::Record;
	bStartOnBeginPlay = true;
}

TArray<FPCGSimulationBodyDesc> APCGSimulationSpikeActor::BuildBodyDescs() const
{
	TArray<FPCGSimulationBodyDesc> Descs;

	const FIntVector Counts(
		FMath::Max(1, GridCounts.X),
		FMath::Max(1, GridCounts.Y),
		FMath::Max(1, GridCounts.Z));

	Descs.Reserve(Counts.X * Counts.Y * Counts.Z);

	const FTransform ActorToWorld = GetActorTransform();
	FRandomStream Random(Seed);

	int32 Ordinal = 0;
	for (int32 Z = 0; Z < Counts.Z; ++Z)
	{
		for (int32 Y = 0; Y < Counts.Y; ++Y)
		{
			for (int32 X = 0; X < Counts.X; ++X)
			{
				FPCGSimulationBodyDesc& Desc = Descs.AddDefaulted_GetRef();
				Desc.Mesh = BodyMesh;

				const FVector LocalOffset = GridOrigin + FVector(
					(X - 0.5 * (Counts.X - 1)) * GridSpacing.X,
					(Y - 0.5 * (Counts.Y - 1)) * GridSpacing.Y,
					Z * GridSpacing.Z);

				Desc.InitialTransform = FTransform(
					FRotator(Random.FRandRange(-20.0f, 20.0f), Random.FRandRange(0.0f, 360.0f), 0.0f),
					ActorToWorld.TransformPosition(LocalOffset));

				Desc.InitialLinearVelocity = FVector(
					Random.FRandRange(-RandomLateralSpeed, RandomLateralSpeed),
					Random.FRandRange(-RandomLateralSpeed, RandomLateralSpeed),
					0.0);

				Desc.InitialAngularVelocity = FVector(
					Random.FRandRange(-RandomSpinDegrees, RandomSpinDegrees),
					Random.FRandRange(-RandomSpinDegrees, RandomSpinDegrees),
					Random.FRandRange(-RandomSpinDegrees, RandomSpinDegrees));

				// The identity the readback probe checks for. In Phase 1 this comes from a point
				// attribute; here it is just something recognisable and non-sequential-looking.
				Desc.SourceId = 1000000 + Ordinal * 7;

				++Ordinal;
			}
		}
	}

	return Descs;
}

void APCGSimulationSpikeActor::BuildRuntimeRepresentation()
{
	using namespace PCGSimulationSpikeActorLocal;

	if (!BodyMesh)
	{
		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: no BodyMesh set - nothing to simulate."), *GetName());
		return;
	}

	TArray<FPCGSimulationBodyDesc> Descs = BuildBodyDescs();
	if (Descs.IsEmpty())
	{
		UE_LOG(LogPCGUtilsSimulation, Error, TEXT("%s: grid produced no bodies."), *GetName());
		return;
	}

	// Keep the initial transforms for the visualization seed before Descs is moved from.
	TArray<FTransform> InitialTransforms;
	InitialTransforms.Reserve(Descs.Num());
	for (const FPCGSimulationBodyDesc& Desc : Descs)
	{
		InitialTransforms.Add(Desc.InitialTransform);
	}

	// --- Floor -------------------------------------------------------------------------------
	if (bCreateFloor)
	{
		FloorComponent = NewObject<UBoxComponent>(this, FloorComponentName);
		FloorComponent->SetMobility(EComponentMobility::Movable);
		FloorComponent->SetupAttachment(GetRootComponent());
		FloorComponent->SetBoxExtent(FloorExtent, /*bUpdateOverlaps*/ false);
		FloorComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		FloorComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorComponent->SetRelativeLocation(FVector(0.0, 0.0, FloorTopZ - FloorExtent.Z));
		FloorComponent->SetHiddenInGame(false);
		FloorComponent->RegisterComponent();
	}

	// --- Bodies ------------------------------------------------------------------------------
	BodiesComponent = NewObject<UPCGSimulationBodiesComponent>(this, BodiesComponentName);
	BodiesComponent->SetMobility(EComponentMobility::Movable);
	BodiesComponent->SetupAttachment(GetRootComponent());
	BodiesComponent->SetBodyDescs(MoveTemp(Descs));

	// RegisterComponent runs OnCreatePhysicsState, which is where the bodies enter the solver. It
	// must happen before FindOrAddObservedComponent so the manager sees a live component.
	BodiesComponent->RegisterComponent();

	// --- Visualization -----------------------------------------------------------------------
	// Separate from the simulated bodies on purpose: an ISM's per-instance bodies are static, so
	// letting it own the physics would defeat the whole exercise.
	VisualizationComponent = NewObject<UInstancedStaticMeshComponent>(this, VisualizationComponentName);
	VisualizationComponent->SetMobility(EComponentMobility::Movable);
	VisualizationComponent->SetupAttachment(GetRootComponent());
	VisualizationComponent->SetStaticMesh(BodyMesh);
	VisualizationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualizationComponent->RegisterComponent();
	VisualizationComponent->AddInstances(InitialTransforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);

	// --- Observation -------------------------------------------------------------------------
	// Explicit deterministic cache name; see ParticipantCacheName's comment.
	FindOrAddObservedComponent(BodiesComponent, ParticipantCacheName, /*bTransferSimulationFlag*/ false);

	// AChaosCacheManager::FindOrAddObservedComponent SILENTLY EARLY-RETURNS for RF_Transient
	// components ("we do not want to record transient component as they may not exists when we
	// start recording"). A plain NewObject does not set that flag, but several component-creation
	// helpers do, and the failure mode is an empty cache with no explanation anywhere. Check it.
	const bool bObserved = GetObservedComponents().ContainsByPredicate(
		[this](const FObservedComponent& Observed)
		{
			return Observed.SoftComponentRef.PathToComponent == BodiesComponent->GetPathName(this);
		});

	if (!bObserved)
	{
		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: bodies component was NOT added to the observed list - nothing will record. "
				 "RF_Transient set? (flag is %s)"),
			*GetName(),
			BodiesComponent->HasAnyFlags(RF_Transient) ? TEXT("SET - that is the cause") : TEXT("not set"));
	}

	UE_LOG(LogPCGUtilsSimulation, Log,
		TEXT("%s: built runtime representation - %d bodies (%d valid), cache name '%s', mode %s."),
		*GetName(),
		BodiesComponent->GetNumBodies(),
		BodiesComponent->GetNumValidBodies(),
		*ParticipantCacheName.ToString(),
		CacheMode == ECacheMode::Record ? TEXT("Record") : TEXT("NotRecord"));

	// (d) R6 probe: the cache manager writes PrimComp->BodyInstance.bSimulatePhysics in several
	// places. For this component that struct is never initialized, so the write should be inert.
	// Log it so a regression is visible rather than mysterious.
	UE_LOG(LogPCGUtilsSimulation, Log,
		TEXT("%s: R6 probe - inherited BodyInstance valid=%s, IsSimulatingPhysics()=%s (both expected false)."),
		*GetName(),
		BodiesComponent->BodyInstance.IsValidBodyInstance() ? TEXT("true") : TEXT("false"),
		BodiesComponent->IsSimulatingPhysics() ? TEXT("true") : TEXT("false"));
}

void APCGSimulationSpikeActor::BeginPlay()
{
	// ORDER IS LOAD-BEARING. AChaosCacheManager::BeginPlay calls Start() -> BeginEvaluate(), which
	// resolves every observed component and opens its cache for record. Components created after
	// that point are invisible to the recording.
	BuildRuntimeRepresentation();

	Super::BeginPlay();
}

void APCGSimulationSpikeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Super runs EndEvaluate() -> EndRecord(), which flushes pending frames into the cache. Read
	// the results after it, not before.
	Super::EndPlay(EndPlayReason);

	if (CacheCollection)
	{
		const UChaosCache* Cache = CacheCollection->FindCache(ParticipantCacheName);

		UE_LOG(LogPCGUtilsSimulation, Log,
			TEXT("%s: EndPlay - collection '%s' dirty=%s. Cache '%s' %s duration=%.3fs frames=%u tracks=%d."),
			*GetName(),
			*CacheCollection->GetName(),
			CacheCollection->GetOutermost()->IsDirty() ? TEXT("YES") : TEXT("NO  <-- (b) FAILED"),
			*ParticipantCacheName.ToString(),
			Cache ? TEXT("found") : TEXT("MISSING <-- (a) FAILED"),
			Cache ? Cache->RecordedDuration : 0.0f,
			Cache ? Cache->NumRecordedFrames : 0u,
			Cache ? Cache->ParticleTracks.Num() : 0);
	}
	else
	{
		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: EndPlay - no CacheCollection assigned, nothing was recorded."), *GetName());
	}
}

void APCGSimulationSpikeActor::TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	// Super drains the physics thread's pending frame queue into the cache. Must not be skipped.
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

	SyncVisualization();
}

void APCGSimulationSpikeActor::SyncVisualization()
{
	if (!BodiesComponent || !VisualizationComponent)
	{
		return;
	}

	const int32 NumBodies = BodiesComponent->GetNumBodies();
	if (VisualizationComponent->GetInstanceCount() != NumBodies)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(NumBodies);

	for (int32 Index = 0; Index < NumBodies; ++Index)
	{
		FTransform BodyTransform;
		if (!BodiesComponent->GetBodyWorldTransform(Index, BodyTransform))
		{
			BodyTransform = BodiesComponent->GetBodyDescs()[Index].InitialTransform;
		}
		Transforms.Add(BodyTransform);
	}

	VisualizationComponent->BatchUpdateInstancesTransforms(
		0, Transforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

UChaosCache* APCGSimulationSpikeActor::ResolveParticipantCache() const
{
	if (!CacheCollection)
	{
		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: no CacheCollection assigned. Create a Chaos Cache Collection asset and assign it."),
			*GetName());
		return nullptr;
	}

	UChaosCache* Cache = CacheCollection->FindCache(ParticipantCacheName);
	if (!Cache)
	{
		FString Available;
		for (const UChaosCache* Existing : CacheCollection->GetCaches())
		{
			if (Existing)
			{
				Available += FString::Printf(TEXT("\n    '%s'"), *Existing->GetName());
			}
		}

		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: no cache named '%s' in '%s'. Record first. Caches present:%s"),
			*GetName(),
			*ParticipantCacheName.ToString(),
			*CacheCollection->GetName(),
			Available.IsEmpty() ? TEXT(" (none)") : *Available);
		return nullptr;
	}

	return Cache;
}

void APCGSimulationSpikeActor::SpikeReportCacheContents()
{
	UChaosCache* Cache = ResolveParticipantCache();
	if (!Cache)
	{
		return;
	}

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("===== PHASE 0 PROBE 1: CACHE CONTENTS ====="));
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  collection      : %s"), *CacheCollection->GetPathName());
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  package dirty   : %s"),
		CacheCollection->GetOutermost()->IsDirty() ? TEXT("yes (unsaved)") : TEXT("no (saved)"));
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  cache           : %s"), *Cache->GetName());
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  RecordedDuration: %.4f s"), Cache->RecordedDuration);
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  NumRecordedFrames: %u"), Cache->NumRecordedFrames);
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  ParticleTracks  : %d"), Cache->ParticleTracks.Num());
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  TrackToParticle : %d entries"), Cache->TrackToParticle.Num());

	// Key counts tell us whether EndRecord's compression pass collapsed settled bodies.
	int32 MinKeys = MAX_int32;
	int32 MaxKeys = 0;
	int64 TotalKeys = 0;
	int32 NumCurveNames = 0;

	for (const FPerParticleCacheData& Track : Cache->ParticleTracks)
	{
		// FParticleTransformTrack::GetNumKeys() and friends are declared without CHAOSCACHING_API,
		// so they do not link from outside the ChaosCaching module. The UPROPERTY members are
		// public, and UChaosCache::Evaluate / EvaluateSingle ARE exported - which is all the
		// readback path actually needs. Noted as R12 in the investigation.
		const int32 NumKeys = Track.TransformData.KeyTimestamps.Num();
		MinKeys = FMath::Min(MinKeys, NumKeys);
		MaxKeys = FMath::Max(MaxKeys, NumKeys);
		TotalKeys += NumKeys;
		NumCurveNames = FMath::Max(NumCurveNames, Track.CurveData.Num());
	}

	if (!Cache->ParticleTracks.IsEmpty())
	{
		UE_LOG(LogPCGUtilsSimulation, Display,
			TEXT("  transform keys  : min=%d max=%d total=%lld (avg %.1f/track)"),
			MinKeys, MaxKeys, TotalKeys, double(TotalKeys) / Cache->ParticleTracks.Num());

		UE_LOG(LogPCGUtilsSimulation, Display,
			TEXT("  per-particle curves: %d names/track (expect 6 - the velocity channels)"), NumCurveNames);
	}

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("==========================================="));
}

void APCGSimulationSpikeActor::SpikeEvaluateOverTime()
{
	using namespace PCGSimulationSpikeActorLocal;

	UChaosCache* Cache = ResolveParticipantCache();
	if (!Cache)
	{
		return;
	}

	// THE gate. UChaosCache::Evaluate early-returns an EMPTY result with only a warning if
	// CurrentPlaybackCount == 0. Without this token the probe would silently report nothing.
	FCacheUserToken Token = Cache->BeginPlayback();
	if (!Token.IsOpen())
	{
		UE_LOG(LogPCGUtilsSimulation, Error,
			TEXT("%s: BeginPlayback failed - the cache is open for record somewhere. Exit PIE first."),
			*GetName());
		return;
	}

	const float Duration = Cache->GetDuration();
	const int32 NumSamples = FMath::Max(2, NumSampleTimes);

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("===== PHASE 0 PROBE 2: ARBITRARY-TIME EVALUATION ====="));
	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  world type      : %d (0=None 1=Game 2=Editor 3=PIE ...) - Editor means (c) is being tested properly"),
		GetWorld() ? int32(GetWorld()->WorldType) : -1);
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  duration        : %.4f s, sampling %d times"), Duration, NumSamples);

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		const float Time = Duration * SampleIndex / float(NumSamples - 1);

		// A FRESH tick record per sample. Reusing one would make event evaluation incremental and
		// would carry LastTime forward - correct for playback, wrong for random access.
		FPlaybackTickRecord TickRecord;
		TickRecord.SetLastTime(Time);

		FCacheEvaluationContext Context(TickRecord);
		Context.bEvaluateTransform = true;
		Context.bEvaluateCurves = true;
		Context.bEvaluateEvents = false;

		const FCacheEvaluationResult Result = Cache->Evaluate(Context, /*MassToLocalTransforms*/ nullptr);

		UE_LOG(LogPCGUtilsSimulation, Display,
			TEXT("  t=%6.3f -> %d transforms, %d particle indices"),
			Time, Result.Transform.Num(), Result.ParticleIndices.Num());

		for (const int32 WantedOrdinal : OrdinalsToPrint)
		{
			// Result arrays are COMPACTED. Find the slot whose ParticleIndex is the ordinal we
			// want - never assume Transform[Ordinal].
			const int32 EvalIndex = Result.ParticleIndices.IndexOfByKey(WantedOrdinal);
			if (EvalIndex == INDEX_NONE)
			{
				UE_LOG(LogPCGUtilsSimulation, Display,
					TEXT("      ordinal %d: not present at this time"), WantedOrdinal);
				continue;
			}

			static const TMap<FName, float> EmptyCurves;
			const TMap<FName, float>& Curves = Result.Curves.IsValidIndex(EvalIndex)
				? Result.Curves[EvalIndex]
				: EmptyCurves;

			const FVector Velocity(
				FindCurve(Curves, FPCGSimulationCacheAdapter::LinearVelocityXName),
				FindCurve(Curves, FPCGSimulationCacheAdapter::LinearVelocityYName),
				FindCurve(Curves, FPCGSimulationCacheAdapter::LinearVelocityZName));

			UE_LOG(LogPCGUtilsSimulation, Display,
				TEXT("      ordinal %d (slot %d): %s  |V|=%.1f cm/s"),
				WantedOrdinal, EvalIndex,
				*FormatTransform(Result.Transform[EvalIndex]),
				Velocity.Size());
		}
	}

	Cache->EndPlayback(Token);

	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  If Z decreases over time and settles, (c) PASSES: arbitrary-time evaluation works"));
	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  with no PIE, no component and no solver."));
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("======================================================"));
}

void APCGSimulationSpikeActor::SpikeVerifyOrdinalIdentity()
{
	UChaosCache* Cache = ResolveParticipantCache();
	if (!Cache)
	{
		return;
	}

	const int32 ExpectedNum = BuildBodyDescs().Num();

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("===== PHASE 0 PROBE 3: ORDINAL IDENTITY ====="));
	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  authored bodies : %d"), ExpectedNum);
	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  recorded tracks : %d"), Cache->TrackToParticle.Num());

	TSet<int32> Seen;
	int32 NumOutOfRange = 0;
	int32 NumDuplicates = 0;

	for (const int32 ParticleIndex : Cache->TrackToParticle)
	{
		if (ParticleIndex < 0 || ParticleIndex >= ExpectedNum)
		{
			++NumOutOfRange;
			continue;
		}

		bool bAlreadySeen = false;
		Seen.Add(ParticleIndex, &bAlreadySeen);
		if (bAlreadySeen)
		{
			++NumDuplicates;
		}
	}

	const int32 NumMissing = ExpectedNum - Seen.Num();

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  distinct ordinals: %d"), Seen.Num());
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  out of range     : %d"), NumOutOfRange);
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  duplicates       : %d"), NumDuplicates);
	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("  never recorded   : %d"), NumMissing);

	const bool bPass = (NumOutOfRange == 0) && (NumDuplicates == 0);
	UE_LOG(LogPCGUtilsSimulation, Display,
		TEXT("  VERDICT: %s - the adapter's ParticleIndex %s survive the round trip."),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bPass ? TEXT("does") : TEXT("does NOT"));

	if (NumMissing > 0)
	{
		UE_LOG(LogPCGUtilsSimulation, Warning,
			TEXT("  %d bodies were never recorded. Expected if bodies were skipped for missing collision; "
				 "investigate otherwise."), NumMissing);
	}

	UE_LOG(LogPCGUtilsSimulation, Display, TEXT("============================================="));
}
