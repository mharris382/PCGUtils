#include "Elements/PCGGetPointData.h"

#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PCGPointProvider.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetPointDataElement"

UPCGGetPointDataSettings::UPCGGetPointDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetPointDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get PCG Point Data");
}

FText UPCGGetPointDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Collects points from actor components implementing PCGPointProvider.");
}
#endif

TArray<FPCGPinProperties> UPCGGetPointDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetPointDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetPointDataElement>();
}

void FPCGGetPointDataElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	const UPCGGetPointDataSettings* GetSettings = CastChecked<UPCGGetPointDataSettings>(Settings);
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

	TInlineComponentArray<UActorComponent*, 8> Components;
	FoundActor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component)
			|| Settings->ComponentSelector.FilterComponent(Component)
			|| !Component->GetClass()->ImplementsInterface(UPCGPointProvider::StaticClass()))
		{
			continue;
		}

		TArray<FPCGPoint> ProviderPoints;
		FPointComponentData ProviderData;
		if (!IPCGPointProvider::Execute_GetPCGPointData(Component, ProviderPoints, ProviderData)
			|| ProviderPoints.IsEmpty())
		{
			continue;
		}

		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
		Points = MoveTemp(ProviderPoints);

		if (UPCGMetadata* Metadata = PointData->MutableMetadata())
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
					Metadata, &GetSettings->ComponentSettings, SceneComponent);
			}

			const bool bUseElementsDomain =
				GetSettings->PointSettings.AttributeDomain == EPCGUtilsPointMetadataDomain::Elements;
			if (bUseElementsDomain)
			{
				for (FPCGPoint& Point : Points)
				{
					Point.MetadataEntry = Metadata->AddEntry();
					UPCGUtilPathDataLibrary::GetPointDataFromSettings(
						Metadata, &GetSettings->PointSettings, &ProviderData,
						PCGMetadataDomainID::Elements, Point.MetadataEntry);
				}
			}
			else
			{
				for (FPCGPoint& Point : Points)
				{
					Point.MetadataEntry = PCGInvalidEntryKey;
				}
				UPCGUtilPathDataLibrary::GetPointDataFromSettings(
					Metadata, &GetSettings->PointSettings, &ProviderData,
					PCGMetadataDomainID::Data, PCGInvalidEntryKey);
			}
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(Component->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
