#include "Elements/Conversion/PCGGetStaticMeshData.h"

#include "Algo/Transform.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/PCGPointData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Geometry/PCGUtilsStaticMeshDynMeshOutput.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetStaticMeshDataElement"

namespace PCGGetStaticMeshDataLocal
{
	bool MatchesStaticMeshCollisionFilter(const UStaticMeshComponent* Component, const UPCGGetStaticMeshDataSettings* Settings)
	{
		if (!Settings->bFilterByCollision)
		{
			return true;
		}

		const bool bCollisionMatches = [Component, Settings]()
		{
			switch (Settings->CollisionRequirement)
			{
			case EPCGUtilsMeshCollisionRequirement::AnyEnabled:
				return Component->IsCollisionEnabled();
			case EPCGUtilsMeshCollisionRequirement::Query:
				return Component->IsQueryCollisionEnabled();
			case EPCGUtilsMeshCollisionRequirement::Physics:
				return Component->IsPhysicsCollisionEnabled();
			case EPCGUtilsMeshCollisionRequirement::QueryAndPhysics:
				return Component->IsQueryCollisionEnabled() && Component->IsPhysicsCollisionEnabled();
			default:
				return false;
			}
		}();

		return bCollisionMatches
			&& (!Settings->bMatchCollisionProfile || Component->GetCollisionProfileName() == Settings->CollisionProfileName)
			&& (!Settings->bMatchObjectType || Component->GetCollisionObjectType() == Settings->ObjectType)
			&& (!Settings->bMatchTraceResponse || (Component->IsQueryCollisionEnabled()
				&& Component->GetCollisionResponseToChannel(Settings->TraceChannel) == Settings->TraceResponse));
	}
}

UPCGGetStaticMeshDataSettings::UPCGGetStaticMeshDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	ComponentSettings.bOutputActorReference = true;
	// Emit the standard "ComponentReference" soft-object-path by default so Paint Static Mesh Vertex Colors
	// (and any component-targeting node) works straight off this node with no attribute setup.
	ComponentSettings.bOutputComponentReference = true;
	// PCG-generated instances are useful source data; the debug tag has its own narrower filter.
	bIgnorePCGGeneratedComponents = false;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetStaticMeshDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Static Mesh Data");
}

FText UPCGGetStaticMeshDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Collects static mesh components and/or instanced static mesh components from actors. Output Mode Points gives one point per static mesh component or one point per ISM instance, in a separate point data per component. Output Mode DynMesh gives the meshes themselves as DynMesh data, grouped by Merge Behavior and optionally self-unioned; unioned results are reused between runs until a mesh, its placement or its asset changes.");
}
#endif

TArray<FPCGPinProperties> UPCGGetStaticMeshDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, GetDataFilter());
	return PinProperties;
}

FPCGElementPtr UPCGGetStaticMeshDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetStaticMeshDataElement>();
}

#if WITH_EDITOR
EPCGChangeType UPCGGetStaticMeshDataSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	// The output pin changes type with the mode.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGGetStaticMeshDataSettings, OutputMode))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

void FPCGGetStaticMeshDataElement::GatherPlacements(const FPCGContext* Context, const UPCGGetStaticMeshDataSettings* GetSettings,
	AActor* FoundActor, TArray<FPCGUtilsStaticMeshPlacement>& OutPlacements)
{
	check(Context && GetSettings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	const FPCGDataFromActorContext* ActorContext = static_cast<const FPCGDataFromActorContext*>(Context);

	TInlineComponentArray<UStaticMeshComponent*, 4> MeshComponents;
	FoundActor->GetComponents(MeshComponents);

	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || !ActorContext->ComponentSelector.FilterComponent(MeshComponent))
		{
			continue;
		}

		const UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(MeshComponent);
		if ((InstancedComponent && GetSettings->MeshSource == EPCGUtilsStaticMeshSource::StaticMeshComponents)
			|| (!InstancedComponent && GetSettings->MeshSource == EPCGUtilsStaticMeshSource::InstancedStaticMeshComponents)
			|| (GetSettings->bFilterPCGDebugComponents && MeshComponent->ComponentHasTag(PCGHelpers::DefaultPCGDebugTag))
			|| (GetSettings->bIgnorePCGGeneratedComponents && MeshComponent->ComponentHasTag(PCGHelpers::DefaultPCGTag))
			|| (GetSettings->bFilterHiddenComponents && (!MeshComponent->IsVisible() || MeshComponent->bHiddenInGame))
			|| !PCGGetStaticMeshDataLocal::MatchesStaticMeshCollisionFilter(MeshComponent, GetSettings))
		{
			continue;
		}

		UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		if (!IsValid(StaticMesh))
		{
			continue;
		}

		if (InstancedComponent)
		{
			const int32 InstanceCount = InstancedComponent->GetInstanceCount();
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (InstancedComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
				{
					OutPlacements.Add({StaticMesh, MeshComponent, InstanceTransform});
				}
			}
		}
		else
		{
			OutPlacements.Add({StaticMesh, MeshComponent, MeshComponent->GetComponentTransform()});
		}
	}
}

void FPCGGetStaticMeshDataElement::ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings,
	const TArray<AActor*>& FoundActors, TArray<FPCGTaskId>& OutDynamicDependencies) const
{
	check(Context && Settings);
	const UPCGGetStaticMeshDataSettings* GetSettings = CastChecked<UPCGGetStaticMeshDataSettings>(Settings);
	if (GetSettings->OutputMode != EPCGUtilsStaticMeshOutput::DynMesh)
	{
		FPCGDataFromActorElement::ProcessActors(Context, Settings, FoundActors, OutDynamicDependencies);
		return;
	}

	// Merging looks across actors, so the DynMesh output takes every found mesh at once.
	TArray<FPCGUtilsStaticMeshPlacement> Placements;
	for (AActor* FoundActor : FoundActors)
	{
		GatherPlacements(Context, GetSettings, FoundActor, Placements);
	}
	if (Placements.IsEmpty())
	{
		return;
	}

	PCGUtilsStaticMeshDynMeshOutput::Emit(Context, GetSettings, Placements);
}

void FPCGGetStaticMeshDataElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	const UPCGGetStaticMeshDataSettings* GetSettings = CastChecked<UPCGGetStaticMeshDataSettings>(Settings);
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

	TArray<FPCGUtilsStaticMeshPlacement> Placements;
	GatherPlacements(Context, GetSettings, FoundActor, Placements);

	// One point data per component: an instanced component's placements are consecutive.
	for (int32 First = 0; First < Placements.Num();)
	{
		UStaticMeshComponent* MeshComponent = Placements[First].Component;
		UStaticMesh* StaticMesh = Placements[First].StaticMesh;
		int32 End = First;
		while (End < Placements.Num() && Placements[End].Component == MeshComponent)
		{
			++End;
		}

		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		const FBox MeshBounds = StaticMesh->GetBoundingBox();
		for (int32 Index = First; Index < End; ++Index)
		{
			FPCGPoint& Point = PointData->GetMutablePoints().Emplace_GetRef();
			Point.Transform = Placements[Index].WorldTransform;
			Point.Density = 1.0f;
			Point.BoundsMin = MeshBounds.Min;
			Point.BoundsMax = MeshBounds.Max;
		}
		First = End;

		if (UPCGMetadata* Metadata = PointData->MutableMetadata())
		{
			if (!GetSettings->MeshOutputAttributeName.IsNone())
			{
				Metadata->FindOrCreateAttribute<FSoftObjectPath>(
					GetSettings->MeshOutputAttributeName, FSoftObjectPath(StaticMesh), false, false, true);
			}

			if (GetSettings->bExtractMeshMaterials && !GetSettings->MaterialOutputAttributeName.IsNone())
			{
				const int32 MaterialCount = FMath::Max(1, GetSettings->MaxMaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					const FName AttributeName = MaterialCount == 1
						? GetSettings->MaterialOutputAttributeName
						: FName(*FString::Printf(TEXT("%s%d"), *GetSettings->MaterialOutputAttributeName.ToString(), MaterialIndex));
					UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
					Metadata->FindOrCreateAttribute<FSoftObjectPath>(
						AttributeName, FSoftObjectPath(Material), false, false, true);
				}
			}

			UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
				Metadata, &GetSettings->ComponentSettings, MeshComponent);
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(MeshComponent->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
