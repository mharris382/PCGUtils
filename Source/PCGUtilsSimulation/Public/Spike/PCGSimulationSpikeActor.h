// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Chaos/CacheManagerActor.h"
#include "PCGSimulationSpikeActor.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UPCGSimulationBodiesComponent;
class UStaticMesh;

/**
 * PHASE 0 SPIKE HARNESS - THROWAWAY.
 *
 * This actor exists to answer four engine questions before any PCG code is written. It hand-authors
 * a grid of bodies so nothing here depends on a PCG graph. Delete it once Phase 1 has a real
 * Build Simulation State; the component and the adapter it drives are keepers, this is not.
 *
 *   (a) Does recording work in Simulate-In-Editor, not just Play-In-Editor?
 *   (b) Is the UChaosCacheCollection asset dirty and savable after exiting?
 *   (c) Can the cache be evaluated at arbitrary times from a COLD editor session - no PIE, no
 *       component, no solver? This is the load-bearing question for the whole feature.
 *   (d) Risk R6: does AChaosCacheManager mishandle a component it cannot treat as single-bodied?
 *       It writes PrimComp->BodyInstance.bSimulatePhysics in several places, which is meaningless
 *       for a multi-body component.
 *
 * RUNBOOK - see Docs/PCGUtilsSimulation_Phase0.md.
 */
UCLASS(Experimental, HideCategories = (Replication, Collision, HLOD, Networking, Input, Actor, Cooking))
class PCGUTILSSIMULATION_API APCGSimulationSpikeActor : public AChaosCacheManager
{
	GENERATED_BODY()

public:
	APCGSimulationSpikeActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ---------------------------------------------------------------------------------------
	// Authoring - stands in for what Build Simulation State will produce in Phase 1.
	// ---------------------------------------------------------------------------------------

	/** Mesh used for every body. Must have simple collision or every body is skipped. */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies")
	TObjectPtr<UStaticMesh> BodyMesh;

	/** Bodies are laid out on this grid. 5x5x4 = 100 bodies, enough to be interesting, fast to record. */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies", meta = (ClampMin = "1"))
	FIntVector GridCounts = FIntVector(5, 5, 4);

	UPROPERTY(EditAnywhere, Category = "Spike|Bodies")
	FVector GridSpacing = FVector(150.0, 150.0, 200.0);

	/** Grid origin, relative to this actor. */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies")
	FVector GridOrigin = FVector(0.0, 0.0, 400.0);

	/** Random per-body spin, degrees/sec. Makes rotation interpolation visible on readback. */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies", meta = (ClampMin = "0.0"))
	float RandomSpinDegrees = 180.0f;

	/** Random per-body horizontal shove, cm/s. Separates the bodies so ordinals are distinguishable. */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies", meta = (ClampMin = "0.0"))
	float RandomLateralSpeed = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Spike|Bodies")
	int32 Seed = 20260831;

	/**
	 * Cache name for the single participant.
	 *
	 * Set explicitly and deterministically. AChaosCacheManager::AddNewObservedComponent would
	 * otherwise assign MakeUniqueObjectName(...), so the name would drift on every rebuild and
	 * orphan every prior recording.
	 */
	UPROPERTY(EditAnywhere, Category = "Spike|Bodies")
	FName ParticipantCacheName = TEXT("PCGSim_Spike_Participant0");

	// ---------------------------------------------------------------------------------------
	// Environment - the V1 "simple floor", minus the AChaosSolverActor.
	// ---------------------------------------------------------------------------------------

	/**
	 * A static box for the bodies to land on.
	 *
	 * NOT the AChaosSolverActor::bHasFloor path from the investigation (4.6). That floor belongs to
	 * a solver actor's own solver, and these bodies go into the world solver; routing them to a
	 * private solver is Phase 1 work and would add risk to a spike that does not need it. None of
	 * the four Phase 0 questions depend on which floor is used.
	 */
	UPROPERTY(EditAnywhere, Category = "Spike|Environment")
	bool bCreateFloor = true;

	UPROPERTY(EditAnywhere, Category = "Spike|Environment")
	FVector FloorExtent = FVector(2000.0, 2000.0, 50.0);

	/** Floor top surface, relative to this actor. */
	UPROPERTY(EditAnywhere, Category = "Spike|Environment")
	double FloorTopZ = 0.0;

	// ---------------------------------------------------------------------------------------
	// Readback probe.
	// ---------------------------------------------------------------------------------------

	/** How many times to sample across the recorded duration in SpikeEvaluateOverTime. */
	UPROPERTY(EditAnywhere, Category = "Spike|Readback", meta = (ClampMin = "2", ClampMax = "64"))
	int32 NumSampleTimes = 6;

	/** Body ordinals whose transform/velocity are printed in full at each sample time. */
	UPROPERTY(EditAnywhere, Category = "Spike|Readback")
	TArray<int32> OrdinalsToPrint = { 0, 1, 42 };

	// ---------------------------------------------------------------------------------------
	// Spike probes. Run these from the details panel, OUTSIDE PIE.
	// ---------------------------------------------------------------------------------------

	/** (b) + inventory: what is actually in the cache collection, and is its package dirty? */
	UFUNCTION(CallInEditor, Category = "Spike|Probes", meta = (DisplayName = "1. Report Cache Contents"))
	void SpikeReportCacheContents();

	/** (c) THE critical probe: BeginPlayback + SetLastTime + Evaluate, with no PIE and no component. */
	UFUNCTION(CallInEditor, Category = "Spike|Probes", meta = (DisplayName = "2. Evaluate Over Time"))
	void SpikeEvaluateOverTime();

	/** Identity: is TrackToParticle a clean permutation of the body ordinals we recorded? */
	UFUNCTION(CallInEditor, Category = "Spike|Probes", meta = (DisplayName = "3. Verify Ordinal Identity"))
	void SpikeVerifyOrdinalIdentity();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
	//~ End AActor interface

private:
	/** Builds the body descriptors the grid settings describe. Deterministic for a given Seed. */
	TArray<struct FPCGSimulationBodyDesc> BuildBodyDescs() const;

	/** Creates components and registers the observed component. Must run BEFORE Super::BeginPlay(). */
	void BuildRuntimeRepresentation();

	/** Pushes live body transforms into the visualization ISM. */
	void SyncVisualization();

	/** Resolves the participant's cache, logging precisely why if it cannot. */
	class UChaosCache* ResolveParticipantCache() const;

	/** Invisible; owns the N simulated bodies. */
	UPROPERTY(Transient)
	TObjectPtr<UPCGSimulationBodiesComponent> BodiesComponent;

	/** Visualization only - never simulated, never observed by the cache manager. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> VisualizationComponent;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> FloorComponent;
};
