// Copyright Max Harris

#include "Components/PCGSimulationBodiesComponent.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PCGUtilsSimulationModule.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSimulationBodiesComponent)

UPCGSimulationBodiesComponent::UPCGSimulationBodiesComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Nothing to draw. Bodies are visualized by a separate ISM.
	SetHiddenInGame(true);
	bVisibleInReflectionCaptures = false;
	bVisibleInRayTracing = false;

	// The inherited single BodyInstance is never initialized (GetBodySetup() is null), but leaving
	// it collidable would be misleading in the details panel.
	BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyInstance.bSimulatePhysics = false;

	// Physics state must be created even though the inherited body setup is null - that is the hook
	// our own bodies are created from.
	bAlwaysCreatePhysicsState = true;
}

void UPCGSimulationBodiesComponent::SetBodyDescs(TArray<FPCGSimulationBodyDesc> InDescs)
{
	BodyDescs = MoveTemp(InDescs);
	UpdateBounds();

	if (IsPhysicsStateCreated())
	{
		RecreatePhysicsState();
	}
}

int32 UPCGSimulationBodiesComponent::GetNumValidBodies() const
{
	int32 Count = 0;
	for (const FBodyInstance* Body : Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			++Count;
		}
	}
	return Count;
}

bool UPCGSimulationBodiesComponent::GetBodyWorldTransform(int32 Index, FTransform& OutTransform) const
{
	if (!Bodies.IsValidIndex(Index) || !Bodies[Index] || !Bodies[Index]->IsValidBodyInstance())
	{
		return false;
	}

	OutTransform = Bodies[Index]->GetUnrealWorldTransform();
	return true;
}

bool UPCGSimulationBodiesComponent::ShouldCreatePhysicsState() const
{
	if (BodyDescs.IsEmpty())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	return World && World->GetPhysicsScene() != nullptr;
}

bool UPCGSimulationBodiesComponent::HasValidPhysicsState() const
{
	return !Bodies.IsEmpty();
}

void UPCGSimulationBodiesComponent::OnCreatePhysicsState()
{
	// Safe to call: UPrimitiveComponent's own body creation is guarded by `if(BodySetup)` and
	// GetBodySetup() is null for this class, so Super only does the bookkeeping we still want
	// (bPhysicsStateCreated, the physics-state-changed broadcast).
	Super::OnCreatePhysicsState();

	CreateBodies();
}

void UPCGSimulationBodiesComponent::OnDestroyPhysicsState()
{
	DestroyBodies();

	Super::OnDestroyPhysicsState();
}

void UPCGSimulationBodiesComponent::CreateBodies()
{
	check(Bodies.IsEmpty());

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysScene)
	{
		return;
	}

	Bodies.Reserve(BodyDescs.Num());

	int32 NumSkipped = 0;

	for (int32 Index = 0; Index < BodyDescs.Num(); ++Index)
	{
		const FPCGSimulationBodyDesc& Desc = BodyDescs[Index];

		UBodySetup* BodySetup = Desc.Mesh ? Desc.Mesh->GetBodySetup() : nullptr;

		// A mesh with no simple collision produces a body with no shapes, which Chaos will accept
		// and then never collide. Refusing it here turns a silent "it fell through the floor" into
		// a log line naming the mesh.
		if (!BodySetup || BodySetup->AggGeom.GetElementCount() == 0)
		{
			UE_LOG(LogPCGUtilsSimulation, Warning,
				TEXT("%s: body %d skipped - mesh '%s' has no simple collision."),
				*GetName(), Index, *GetNameSafe(Desc.Mesh));

			Bodies.Add(nullptr);
			++NumSkipped;
			continue;
		}

		FBodyInstance* Body = new FBodyInstance();
		Body->bAutoWeld = false;
		Body->bSimulatePhysics = true;
		Body->bStartAwake = true;
		Body->bEnableGravity = Desc.bEnableGravity;
		Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		if (Desc.MassOverrideKg > 0.0f)
		{
			Body->SetMassOverride(Desc.MassOverrideKg, /*bNewOverrideMass*/ true);
		}

		// bPhysicsTypeDeterminesSimulation=false: honour bSimulatePhysics above rather than letting
		// the body setup's PhysicsType decide. Static meshes default to PhysType_Default, which
		// would otherwise leave these kinematic.
		const FInitBodySpawnParams SpawnParams(/*bStaticPhysics*/ false, /*bPhysicsTypeDeterminesSimulation*/ false);

		Body->InitBody(BodySetup, Desc.InitialTransform, this, PhysScene, SpawnParams);

		if (!Body->IsValidBodyInstance())
		{
			UE_LOG(LogPCGUtilsSimulation, Warning,
				TEXT("%s: body %d failed to initialize (mesh '%s')."),
				*GetName(), Index, *GetNameSafe(Desc.Mesh));

			delete Body;
			Bodies.Add(nullptr);
			++NumSkipped;
			continue;
		}

		// Initial velocities must be applied after scene addition - before InitBody there is no
		// particle to write to.
		if (!Desc.InitialLinearVelocity.IsNearlyZero())
		{
			Body->SetLinearVelocity(Desc.InitialLinearVelocity, /*bAddToCurrent*/ false);
		}

		if (!Desc.InitialAngularVelocity.IsNearlyZero())
		{
			Body->SetAngularVelocityInRadians(
				FMath::DegreesToRadians(Desc.InitialAngularVelocity), /*bAddToCurrent*/ false);
		}

		Bodies.Add(Body);
	}

	UE_LOG(LogPCGUtilsSimulation, Log,
		TEXT("%s: created %d/%d bodies (%d skipped)."),
		*GetName(), GetNumValidBodies(), BodyDescs.Num(), NumSkipped);
}

void UPCGSimulationBodiesComponent::DestroyBodies()
{
	for (FBodyInstance* Body : Bodies)
	{
		if (Body)
		{
			Body->TermBody();
			delete Body;
		}
	}

	Bodies.Reset();
}

FBoxSphereBounds UPCGSimulationBodiesComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds are only used for culling and the editor's focus/selection box; the component draws
	// nothing. Union of the authored start transforms is close enough and, unlike a live query,
	// costs nothing per frame and is valid before physics exists.
	if (BodyDescs.IsEmpty())
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0f);
	}

	FBox Box(ForceInit);
	for (const FPCGSimulationBodyDesc& Desc : BodyDescs)
	{
		const FVector Extent = Desc.Mesh
			? Desc.Mesh->GetBounds().BoxExtent * Desc.InitialTransform.GetScale3D()
			: FVector(50.0);

		Box += FBox::BuildAABB(Desc.InitialTransform.GetLocation(), Extent);
	}

	return FBoxSphereBounds(Box);
}
