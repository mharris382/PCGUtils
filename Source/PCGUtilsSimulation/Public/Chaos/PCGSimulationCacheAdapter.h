// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Adapters/CacheAdapter.h"

/**
 * Records every body of a UPCGSimulationBodiesComponent into ONE UChaosCache, one track per body.
 *
 * The whole point is the particle index. UChaosCache::FlushPendingFrames grows TrackToParticle from
 * whatever integer the adapter puts in FPendingParticleWrite::ParticleIndex:
 *
 *     if (!TrackToParticle.Find(ParticleIndex, TrackIndex)) { TrackToParticle.Add(ParticleIndex); ... }
 *
 * so an adapter that writes `ParticleIndex = ordinal within the participant` gets that ordinal back
 * out of FCacheEvaluationResult::ParticleIndices at readback time, unchanged, forever. That is the
 * link the engine gives us for free; everything from the ordinal back to a PCG point is ours to
 * persist. FGeometryCollectionCacheAdapter does exactly the same thing with bone indices.
 *
 * Registered as a modular feature by FPCGUtilsSimulationModule.
 */
class PCGUTILSSIMULATION_API FPCGSimulationCacheAdapter : public Chaos::FComponentCacheAdapter
{
public:
	virtual ~FPCGSimulationCacheAdapter() = default;

	/** Per-particle curve names written alongside each transform key. */
	static const FName LinearVelocityXName;
	static const FName LinearVelocityYName;
	static const FName LinearVelocityZName;
	static const FName AngularVelocityXName;
	static const FName AngularVelocityYName;
	static const FName AngularVelocityZName;

	//~ Begin FComponentCacheAdapter interface
	virtual SupportType SupportsComponentClass(UClass* InComponentClass) const override;
	virtual UClass* GetDesiredClass() const override;
	virtual uint8 GetPriority() const override;
	virtual FGuid GetGuid() const override;

	virtual Chaos::FPhysicsSolver* GetComponentSolver(UPrimitiveComponent* InComponent) const override;
	virtual Chaos::FPhysicsSolverEvents* BuildEventsSolver(UPrimitiveComponent* InComponent) const override;

	virtual bool ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;

	virtual bool InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache) override;
	virtual bool InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache, float InTime) override;

	virtual void Record_PostSolve(
		UPrimitiveComponent* InComponent,
		const FTransform& InRootTransform,
		FPendingFrameWrite& OutFrame,
		Chaos::FReal InTime) const override;

	virtual void Playback_PreSolve(
		UPrimitiveComponent* InComponent,
		UChaosCache* InCache,
		Chaos::FReal InTime,
		FPlaybackTickRecord& TickRecord,
		TArray<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const override;

	virtual void SetRestState(
		UPrimitiveComponent* InComponent,
		UChaosCache* InCache,
		const FTransform& InRootTransform,
		Chaos::FReal InTime) const override;
	//~ End FComponentCacheAdapter interface
};
