// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PCGSimulationBodiesComponent.generated.h"

class UStaticMesh;
struct FBodyInstance;

/**
 * One rigid body in a simulation participant.
 *
 * This is the frozen, fully-resolved per-body description: no attribute selectors, no PCG data, no
 * lookups. Phase 1's Build Simulation State produces an array of these; Phase 0 hand-authors them.
 */
USTRUCT(BlueprintType)
struct PCGUTILSSIMULATION_API FPCGSimulationBodyDesc
{
	GENERATED_BODY()

	/** Collision comes from this mesh's UBodySetup. A mesh with no simple collision is skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	TObjectPtr<UStaticMesh> Mesh;

	/** World-space transform at simulation start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	FVector InitialLinearVelocity = FVector::ZeroVector;

	/** Degrees per second. Converted to radians when applied - Chaos stores radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	FVector InitialAngularVelocity = FVector::ZeroVector;

	/** Kilograms. Zero means "derive from the mesh's body setup". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.0"))
	float MassOverrideKg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bEnableGravity = true;

	/**
	 * Stable identity for this body, carried through to readback.
	 *
	 * PHASE 0 does not exercise this - it exists so the spike's logging can prove the ordinal
	 * survives the round trip, which is the half of the identity chain (investigation 4.4) that
	 * Chaos does NOT supply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	int64 SourceId = 0;
};

/**
 * A primitive component that owns N independent dynamic rigid bodies.
 *
 * WHY THIS EXISTS - the finding that forced it:
 *
 * FStaticMeshCacheAdapter::Record_PostSolve writes exactly one particle per component, hard-coded:
 *
 *     NewData.ParticleIndex = 0; // Only one particle for static caches
 *
 * So driving 400 scattered points through the engine's static-mesh path costs 400 components and
 * 400 UChaosCache objects, and - fatally for the identity design - leaves no way to choose the
 * particle index. UChaosCache::TrackToParticle is built from whatever integer the adapter supplies,
 * so controlling that integer is what lets a cache track map back to a PCG element. One component
 * with N bodies and a matching adapter is the only route to that.
 *
 * IMPLEMENTATION: bodies are raw FBodyInstance*, not UPROPERTY - the same pattern
 * USkeletalMeshComponent uses for its physics-asset bodies. GetBodySetup() stays null, which makes
 * UPrimitiveComponent's inherited single-BodyInstance path inert (it is guarded by `if(BodySetup)`),
 * so Super:: can still be called normally on both physics-state hooks.
 *
 * The component is deliberately invisible: CreateSceneProxy() is left returning nullptr and
 * visualization is somebody else's job. Rendering N bodies is an ISM's problem, not a physics
 * component's.
 */
UCLASS(ClassGroup = "PCG", meta = (BlueprintSpawnableComponent), BlueprintType)
class PCGUTILSSIMULATION_API UPCGSimulationBodiesComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPCGSimulationBodiesComponent();

	/**
	 * Replaces the body set. Recreates physics state if it already existed, so this is safe to call
	 * on a registered component.
	 */
	void SetBodyDescs(TArray<FPCGSimulationBodyDesc> InDescs);

	const TArray<FPCGSimulationBodyDesc>& GetBodyDescs() const { return BodyDescs; }

	int32 GetNumBodies() const { return BodyDescs.Num(); }

	/**
	 * Live body instances, parallel to GetBodyDescs(). Entries are null where a body could not be
	 * created (missing mesh, or a mesh with no simple collision).
	 *
	 * The cache adapter reads this from the PHYSICS THREAD to reach each body's particle handle -
	 * the same access pattern FStaticMeshCacheAdapter uses on MeshComp->BodyInstance. The array
	 * itself is only ever resized on the game thread, between solver ticks.
	 */
	const TArray<FBodyInstance*>& GetBodies() const { return Bodies; }

	/** Game-thread read of a body's current world transform. Returns false for a missing body. */
	bool GetBodyWorldTransform(int32 Index, FTransform& OutTransform) const;

	/** How many bodies actually made it into the solver. Diagnostic for the spike report. */
	int32 GetNumValidBodies() const;

	//~ Begin UPrimitiveComponent interface
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent interface

private:
	void CreateBodies();
	void DestroyBodies();

	UPROPERTY(EditAnywhere, Category = "Simulation")
	TArray<FPCGSimulationBodyDesc> BodyDescs;

	/** Not a UPROPERTY: FBodyInstance is not a UObject. Manually owned; see DestroyBodies. */
	TArray<FBodyInstance*> Bodies;
};
