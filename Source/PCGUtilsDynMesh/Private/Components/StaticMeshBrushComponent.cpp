// Copyright Max Harris

#include "Components/StaticMeshBrushComponent.h"

#include "Engine/CollisionProfile.h"
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
}
#endif

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
