#include "Elements/PCGGetPathData.h"

#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PCGPathProvider.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetPathDataElement"

UPCGGetPathDataSettings::UPCGGetPathDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetPathDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get PCG Path Data");
}

FText UPCGGetPathDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Collects point paths from actor components implementing PCGPathProvider.");
}
#endif

TArray<FPCGPinProperties> UPCGGetPathDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetPathDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetPathElement>();
}

void FPCGGetPathElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	const UPCGGetPathDataSettings* GetSettings = CastChecked<UPCGGetPathDataSettings>(Settings);
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

	TInlineComponentArray<UActorComponent*, 8> Components;
	FoundActor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component)
			|| Settings->ComponentSelector.FilterComponent(Component)
			|| !Component->GetClass()->ImplementsInterface(UPCGPathProvider::StaticClass()))
		{
			continue;
		}

		TArray<FPCGPoint> ProviderPoints = IPCGPathProvider::Execute_GetPathPoints(Component);
		if (ProviderPoints.IsEmpty())
		{
			continue;
		}

		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (SceneComponent)
		{
			const FTransform ComponentTransform = SceneComponent->GetComponentTransform();
			for (FPCGPoint& Point : ProviderPoints)
			{
				Point.Transform = Point.Transform * ComponentTransform;
			}
		}

		const FPathComponentData PathData = IPCGPathProvider::Execute_GetPathData(Component);
		const bool bIsClosed = IPCGPathProvider::Execute_GetIsClosedLoop(Component);
		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		PointData->GetMutablePoints() = MoveTemp(ProviderPoints);

		if (UPCGMetadata* Metadata = PointData->MutableMetadata())
		{
			if (FPCGMetadataAttribute<bool>* IsClosedAttribute =
				Metadata->FindOrCreateAttribute<bool>(
					FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data),
					bIsClosed, false, false, true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, bIsClosed);
			}

			if (SceneComponent)
			{
				UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
					Metadata, &GetSettings->ComponentSettings, SceneComponent);
			}
			UPCGUtilPathDataLibrary::GetPathDataFromSettings(
				Metadata, &GetSettings->PathSettings, &PathData);
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(Component->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
