#include "Elements/PCGGetStaticMeshData.h"

#include "Algo/Transform.h"
#include "Components/StaticMeshComponent.h"
#include "Data/PCGPointData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetStaticMeshDataElement"

UPCGGetStaticMeshDataSettings::UPCGGetStaticMeshDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	ComponentSettings.bOutputActorReference = true;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetStaticMeshDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Static Mesh Data");
}

FText UPCGGetStaticMeshDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Collects static mesh components from actors and outputs one point data collection per component.");
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
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

	TInlineComponentArray<UStaticMeshComponent*, 4> MeshComponents;
	FoundActor->GetComponents(MeshComponents);

	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		FPCGPoint& Point = PointData->GetMutablePoints().Emplace_GetRef();
		Point.Transform = MeshComponent->GetComponentTransform();
		Point.Density = 1.0f;

		if (StaticMesh)
		{
			const FBox MeshBounds = StaticMesh->GetBoundingBox();
			Point.BoundsMin = MeshBounds.Min;
			Point.BoundsMax = MeshBounds.Max;
		}
		else
		{
			Point.BoundsMin = FVector::ZeroVector;
			Point.BoundsMax = FVector::ZeroVector;
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
