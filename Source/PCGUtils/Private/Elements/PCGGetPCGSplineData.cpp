#include "Elements/PCGGetPCGSplineData.h"

#include "Components/PCGSplineComponent.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"
#include "OverrideGraphs.h"
#include "Algo/Transform.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "PCGGetPCGSplineDataElement"

UPCGGetPCGSplineDataSettings::UPCGGetPCGSplineDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
}

#if WITH_EDITOR
FText UPCGGetPCGSplineDataSettings::GetDefaultNodeTitle() const
{
	return NSLOCTEXT("PCGGetPCGSplineDataElement", "NodeTitle", "Get PCG Spline Data");
}

FText UPCGGetPCGSplineDataSettings::GetNodeTooltipText() const
{
	return NSLOCTEXT("PCGGetPCGSplineDataElement", "NodeTooltip",
		"Extends Get Spline Data for UPCGSplineComponent by optionally writing Height and PreProcessSplineGraph attributes to the @Data domain.");
}
#endif

FPCGElementPtr UPCGGetPCGSplineDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetPCGSplineDataElement>();
}

void FPCGGetPCGSplineDataElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);

	if (!FoundActor || !IsValid(FoundActor))
	{
		return;
	}

	const UPCGGetPCGSplineDataSettings* SplineSettings = CastChecked<UPCGGetPCGSplineDataSettings>(Settings);

	auto NameTagsToStringTags = [](const FName& InName) { return InName.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameTagsToStringTags);

	TInlineComponentArray<USplineComponent*, 4> SplineComponents;
	FoundActor->GetComponents(SplineComponents);

	for (USplineComponent* SplineComponent : SplineComponents)
	{
		if (!SplineComponent || !IsValid(SplineComponent))
		{
			continue;
		}
		UPCGSplineComponent* Comp = Cast<UPCGSplineComponent>(SplineComponent);
		
		if (!Comp)
			continue;

		UPCGSplineData* SplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
		SplineData->Initialize(SplineComponent);

		if (Comp && SplineData->Metadata)
		{
			if (SplineSettings->bExtractHeight && !SplineSettings->HeightAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<float>* HeightAttribute =
					SplineData->Metadata->FindOrCreateAttribute<float>(
						FPCGAttributeIdentifier(FName(*SplineSettings->HeightAttributeName), PCGMetadataDomainID::Data),
						0.0f,
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
				{
					HeightAttribute->SetValue(PCGInvalidEntryKey, Comp->Height);
				}
			}

			if (SplineSettings->bExtractPreProcessSplineGraph && !SplineSettings->PreProcessSplineGraphAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
					SplineData->Metadata->FindOrCreateAttribute<FSoftObjectPath>(
						FPCGAttributeIdentifier(FName(*SplineSettings->PreProcessSplineGraphAttributeName), PCGMetadataDomainID::Data),
						Comp->PreProcessSplineGraph.GetOverrideGraphInterface(),
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
				{
					GraphAttribute->SetValue(PCGInvalidEntryKey, Comp->PreProcessSplineGraph.GetOverrideGraphInterface());
				}
			}
			
			if (SplineSettings -> bExtractGroup && !SplineSettings->GroupAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<int32>* GroupAttribute =
					SplineData->Metadata->FindOrCreateAttribute<int32>(
						FPCGAttributeIdentifier(FName(*SplineSettings->GroupAttributeName), PCGMetadataDomainID::Data),
						Comp->GroupID,
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
				{
					GroupAttribute->SetValue(PCGInvalidEntryKey, Comp->GroupID);
				}
			}
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = SplineData;
		Algo::Transform(SplineComponent->ComponentTags, TaggedData.Tags, NameTagsToStringTags);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
