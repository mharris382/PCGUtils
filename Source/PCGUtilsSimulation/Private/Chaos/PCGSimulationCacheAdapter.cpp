// Copyright Max Harris

#include "Chaos/PCGSimulationCacheAdapter.h"

#include "Chaos/ChaosCache.h"
#include "Components/PCGSimulationBodiesComponent.h"
#include "Engine/World.h"
#include "Framework/Threading.h"
#include "PBDRigidsSolver.h"
#include "PCGUtilsSimulationModule.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

const FName FPCGSimulationCacheAdapter::LinearVelocityXName(TEXT("PCGSim_LinearVelocityX"));
const FName FPCGSimulationCacheAdapter::LinearVelocityYName(TEXT("PCGSim_LinearVelocityY"));
const FName FPCGSimulationCacheAdapter::LinearVelocityZName(TEXT("PCGSim_LinearVelocityZ"));
const FName FPCGSimulationCacheAdapter::AngularVelocityXName(TEXT("PCGSim_AngularVelocityX"));
const FName FPCGSimulationCacheAdapter::AngularVelocityYName(TEXT("PCGSim_AngularVelocityY"));
const FName FPCGSimulationCacheAdapter::AngularVelocityZName(TEXT("PCGSim_AngularVelocityZ"));

Chaos::FComponentCacheAdapter::SupportType FPCGSimulationCacheAdapter::SupportsComponentClass(UClass* InComponentClass) const
{
	UClass* Desired = GetDesiredClass();

	if (InComponentClass == Desired)
	{
		return SupportType::Direct;
	}

	if (InComponentClass && InComponentClass->IsChildOf(Desired))
	{
		return SupportType::Derived;
	}

	return SupportType::None;
}

UClass* FPCGSimulationCacheAdapter::GetDesiredClass() const
{
	return UPCGSimulationBodiesComponent::StaticClass();
}

uint8 FPCGSimulationCacheAdapter::GetPriority() const
{
	// Above UserAdapterPriorityBegin so nothing in the engine can outrank us for our own component
	// class. Belt and braces - no engine adapter claims UPCGSimulationBodiesComponent anyway, since
	// it does not derive from UStaticMeshComponent.
	return UserAdapterPriorityBegin;
}

FGuid FPCGSimulationCacheAdapter::GetGuid() const
{
	// ============================ DO NOT EVER CHANGE THIS GUID ============================
	// It is stamped into every UChaosCache this adapter records (UChaosCache::AdapterGuid) and is
	// how a cache is matched back to its adapter on replay. Changing it permanently invalidates
	// every recording ever made with this plugin, silently.
	//
	// Built from components rather than FGuid::Parse inside checkSlow (the engine adapters' idiom),
	// because checkSlow compiles out in non-debug builds and would leave the guid default-invalid.
	// {7F3A9C2E-5B14-486D-A0E6-C81D4F927B35}
	// ======================================================================================
	return FGuid(0x7F3A9C2E, 0x5B14486D, 0xA0E6C81D, 0x4F927B35);
}

Chaos::FPhysicsSolver* FPCGSimulationCacheAdapter::GetComponentSolver(UPrimitiveComponent* InComponent) const
{
	if (InComponent && InComponent->GetWorld())
	{
		if (FPhysScene* WorldScene = InComponent->GetWorld()->GetPhysicsScene())
		{
			return WorldScene->GetSolver();
		}
	}

	return nullptr;
}

Chaos::FPhysicsSolverEvents* FPCGSimulationCacheAdapter::BuildEventsSolver(UPrimitiveComponent* InComponent) const
{
	return GetComponentSolver(InComponent);
}

bool FPCGSimulationCacheAdapter::ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const
{
	const UPCGSimulationBodiesComponent* Comp = Cast<UPCGSimulationBodiesComponent>(InComponent);
	if (!Comp || !InCache)
	{
		return false;
	}

	// Deliberately permissive, matching FGeometryCollectionCacheAdapter: every recorded track only
	// has to name a body ordinal this component actually has. A component that gained bodies since
	// recording still plays back the ones it had.
	const int32 NumBodies = Comp->GetNumBodies();
	for (const int32 ParticleIndex : InCache->TrackToParticle)
	{
		if (ParticleIndex < 0 || ParticleIndex >= NumBodies)
		{
			return false;
		}
	}

	return InCache->TrackToParticle.Num() > 0;
}

bool FPCGSimulationCacheAdapter::InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache)
{
	// Nothing to do: the component created its bodies simulating. Mirrors
	// FStaticMeshCacheAdapter::InitializeForRecord.
	return true;
}

bool FPCGSimulationCacheAdapter::InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache, float InTime)
{
	Chaos::EnsureIsInGameThreadContext();

	// Playback drives bodies from the cache, so they must stop solving. Same treatment
	// FStaticMeshCacheAdapter gives its single body, applied per body.
	if (UPCGSimulationBodiesComponent* Comp = Cast<UPCGSimulationBodiesComponent>(InComponent))
	{
		for (FBodyInstance* Body : Comp->GetBodies())
		{
			if (Body && Body->IsValidBodyInstance())
			{
				if (FPhysicsActorHandle Handle = Body->GetPhysicsActor())
				{
					FPhysInterface_Chaos::SetIsKinematic_AssumesLocked(Handle, true);
				}
			}
		}
	}

	return true;
}

void FPCGSimulationCacheAdapter::Record_PostSolve(
	UPrimitiveComponent* InComponent,
	const FTransform& InRootTransform,
	FPendingFrameWrite& OutFrame,
	Chaos::FReal InTime) const
{
	using namespace Chaos;

	EnsureIsInPhysicsThreadContext();

	const UPCGSimulationBodiesComponent* Comp = CastChecked<UPCGSimulationBodiesComponent>(InComponent);

	const TArray<FBodyInstance*>& Bodies = Comp->GetBodies();
	const FTransform WorldToRoot = InRootTransform.Inverse();

	OutFrame.PendingParticleData.Reserve(Bodies.Num());

	for (int32 Ordinal = 0; Ordinal < Bodies.Num(); ++Ordinal)
	{
		FBodyInstance* Body = Bodies[Ordinal];
		if (!Body)
		{
			continue;
		}

		// Reaching the particle handle through the proxy is the same route
		// FStaticMeshCacheAdapter::Record_PostSolve takes from MeshComp->BodyInstance.
		FSingleParticlePhysicsProxy* Proxy = Body->GetPhysicsActor();
		if (!Proxy)
		{
			continue;
		}

		TGeometryParticleHandle<FReal, 3>* Handle = Proxy->GetHandle_LowLevel();
		FPBDRigidParticleHandle* AsRigid = Handle ? Handle->CastToRigidParticle() : nullptr;
		if (!AsRigid)
		{
			continue;
		}

		if (AsRigid->Disabled())
		{
			continue;
		}

		FPendingParticleWrite& Pending = OutFrame.PendingParticleData.AddDefaulted_GetRef();

		// THE line this whole class exists for. The ordinal is the identity.
		Pending.ParticleIndex = Ordinal;

		// Recorded in cache-manager space, like every engine adapter. Keeping transforms relative to
		// the capture actor is also what keeps the float32 key storage
		// (FRawAnimSequenceTrack: FVector3f/FQuat4f) precise at large world coordinates.
		Pending.PendingTransform = FTransform(AsRigid->GetR(), AsRigid->GetX()).GetRelativeTransform(InRootTransform);

		// Velocity is not recorded by ANY engine adapter - transforms only. Per-particle curves are
		// the supported channel for it (FPerParticleCacheData::CurveData), and having it means
		// readback does not have to finite-difference two evaluations.
		const FVec3 V = WorldToRoot.TransformVector(AsRigid->GetV());
		const FVec3 W = AsRigid->GetW();

		Pending.PendingCurveData.Reserve(6);
		Pending.PendingCurveData.Emplace(LinearVelocityXName, static_cast<float>(V.X));
		Pending.PendingCurveData.Emplace(LinearVelocityYName, static_cast<float>(V.Y));
		Pending.PendingCurveData.Emplace(LinearVelocityZName, static_cast<float>(V.Z));
		Pending.PendingCurveData.Emplace(AngularVelocityXName, static_cast<float>(W.X));
		Pending.PendingCurveData.Emplace(AngularVelocityYName, static_cast<float>(W.Y));
		Pending.PendingCurveData.Emplace(AngularVelocityZName, static_cast<float>(W.Z));
	}
}

void FPCGSimulationCacheAdapter::Playback_PreSolve(
	UPrimitiveComponent* InComponent,
	UChaosCache* InCache,
	Chaos::FReal InTime,
	FPlaybackTickRecord& TickRecord,
	TArray<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const
{
	using namespace Chaos;

	UPCGSimulationBodiesComponent* Comp = CastChecked<UPCGSimulationBodiesComponent>(InComponent);

	if (!InCache || InCache->GetDuration() == 0.0f)
	{
		return;
	}

	const TArray<FBodyInstance*>& Bodies = Comp->GetBodies();

	FCacheEvaluationContext Context(TickRecord);
	Context.bEvaluateTransform = true;
	Context.bEvaluateCurves = false;
	Context.bEvaluateEvents = false;

	// No MassToLocal: unlike geometry collections, these are single-shape rigid bodies whose
	// recorded transform is already the particle transform.
	const FCacheEvaluationResult Result = InCache->Evaluate(Context, nullptr);

	for (int32 EvalIndex = 0; EvalIndex < Result.Transform.Num(); ++EvalIndex)
	{
		// Result arrays are COMPACTED - tracks that have not begun or have deactivated are skipped -
		// so EvalIndex is not the body ordinal. ParticleIndices is the authoritative mapping.
		const int32 Ordinal = Result.ParticleIndices.IsValidIndex(EvalIndex)
			? Result.ParticleIndices[EvalIndex]
			: INDEX_NONE;

		if (!Bodies.IsValidIndex(Ordinal) || !Bodies[Ordinal])
		{
			continue;
		}

		FSingleParticlePhysicsProxy* Proxy = Bodies[Ordinal]->GetPhysicsActor();
		TGeometryParticleHandle<FReal, 3>* Handle = Proxy ? Proxy->GetHandle_LowLevel() : nullptr;
		if (!Handle || Handle->ObjectState() != EObjectStateType::Kinematic)
		{
			continue;
		}

		if (FPBDRigidParticleHandle* AsRigid = Handle->CastToRigidParticle())
		{
			const FTransform WorldTransform = Result.Transform[EvalIndex] * TickRecord.GetSpaceTransform();
			AsRigid->SetX(WorldTransform.GetTranslation());
			AsRigid->SetR(WorldTransform.GetRotation());
			OutUpdatedRigids.Add(AsRigid);
		}
	}
}

void FPCGSimulationCacheAdapter::SetRestState(
	UPrimitiveComponent* InComponent,
	UChaosCache* InCache,
	const FTransform& InRootTransform,
	Chaos::FReal InTime) const
{
	Chaos::EnsureIsInGameThreadContext();

	if (!InCache || InCache->GetDuration() == 0.0f)
	{
		return;
	}

	// Static-pose scrubbing has nothing to move: the component draws nothing and its bodies do not
	// exist outside PIE. Visualization owns the editor-time pose. Left as an explicit no-op rather
	// than unimplemented() so the cache manager's ECacheMode::None path stays safe.
}
