#include "Elements/PCGGetStaticMeshData.h"

#include "Algo/Transform.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/PCGPointData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetStaticMeshDataElement"

namespace
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
	return LOCTEXT("NodeTooltip", "Collects static mesh components and/or instanced static mesh components from actors. Outputs one point per static mesh component or one point per ISM instance, in a separate point data collection per component.");
}
#endif

TArray<FPCGPinProperties> UPCGGetStaticMeshDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetStaticMeshDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetStaticMeshDataElement>();
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
	const FPCGDataFromActorContext* ActorContext = static_cast<const FPCGDataFromActorContext*>(Context);
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

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
			|| !MatchesStaticMeshCollisionFilter(MeshComponent, GetSettings))
		{
			continue;
		}

		UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		if (!IsValid(StaticMesh))
		{
			continue;
		}
		const int32 InstanceCount = InstancedComponent ? InstancedComponent->GetInstanceCount() : 0;
		if (InstancedComponent && InstanceCount == 0)
		{
			continue;
		}

		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		const FBox MeshBounds = StaticMesh->GetBoundingBox();
		auto AddPoint = [PointData, &MeshBounds](const FTransform& Transform)
		{
			FPCGPoint& Point = PointData->GetMutablePoints().Emplace_GetRef();
			Point.Transform = Transform;
			Point.Density = 1.0f;
			Point.BoundsMin = MeshBounds.Min;
			Point.BoundsMax = MeshBounds.Max;
		};

		if (InstancedComponent)
		{
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (InstancedComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
				{
					AddPoint(InstanceTransform);
				}
			}
		}
		else
		{
			AddPoint(MeshComponent->GetComponentTransform());
		}

		if (PointData->GetPoints().IsEmpty())
		{
			continue;
		}

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
