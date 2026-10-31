// Copyright Max Harris

#include "Components/StaticMeshBrushComponent.h"

#include "Components/StaticMeshBrushCombinerComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Settings/PCGUtilsDynMeshSettings.h"

UStaticMeshBrushComponent::UStaticMeshBrushComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ApplyBrushDefaults();
	this->bIsEditorOnly = true;
	this->bHiddenInGame = true;
	this->bHiddenInSceneCapture = true;
}

void UStaticMeshBrushComponent::OnRegister()
{
	Super::OnRegister();
	ApplyBrushDefaults();
}

void UStaticMeshBrushComponent::PostLoad()
{
	Super::PostLoad();
	ApplyBrushDefaults();
}

#if WITH_EDITOR
void UStaticMeshBrushComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyBrushDefaults();

	// Interactive drags of a numeric field send many changes; recombine once the value is committed.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		NotifyCombiner();
	}
}

void UStaticMeshBrushComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		NotifyCombiner();
	}
}

void UStaticMeshBrushComponent::PostEditUndo()
{
	Super::PostEditUndo();
	NotifyCombiner();
}
#endif

TSharedPtr<const FStaticMeshBrushCachedMesh> UStaticMeshBrushComponent::GetProcessedMesh(FText& OutError) const
{
	return FStaticMeshBrushCache::Get().FindOrBuildBrushMesh(
		GetStaticMesh(), GetComponentTransform().GetScale3D(), Processing, OutError);
}

FTransform UStaticMeshBrushComponent::GetBrushPlacement() const
{
	const FTransform& World = GetComponentTransform();
	return FTransform(World.GetRotation(), World.GetLocation());
}

FStaticMeshBrushMeshKey UStaticMeshBrushComponent::MakeProcessedMeshKey() const
{
	return FStaticMeshBrushCache::MakeMeshKey(GetStaticMesh(), GetComponentTransform().GetScale3D(), Processing);
}

void UStaticMeshBrushComponent::RebuildProcessedMesh()
{
	if (UStaticMesh* Mesh = GetStaticMesh())
	{
		// Invalidation broadcasts to combiners, which recombine if they are set to.
		FStaticMeshBrushCache::Get().InvalidateMesh(Mesh);
		FText Error;
		GetProcessedMesh(Error);
	}
}

void UStaticMeshBrushComponent::NotifyCombiner() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (UStaticMeshBrushCombinerComponent* Combiner = Owner->FindComponentByClass<UStaticMeshBrushCombinerComponent>())
		{
			Combiner->NotifyBrushesChanged();
		}
	}
}

void UStaticMeshBrushComponent::ApplyBrushDefaults()
{
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);

	const UPCGUtilsDynMeshSettings* Settings = GetDefault<UPCGUtilsDynMeshSettings>();
	if (!Settings || Settings->StaticMeshBrushMaterial.IsNull())
	{
		return;
	}

	UMaterialInterface* BrushMaterial = Settings->StaticMeshBrushMaterial.LoadSynchronous();
	if (!BrushMaterial)
	{
		return;
	}

	const int32 MaterialSlotCount = FMath::Max(1, GetNumMaterials());
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotCount; ++MaterialIndex)
	{
		SetMaterial(MaterialIndex, BrushMaterial);
	}
}
