#include "Elements/PCGGetPCGSplineData.h"

#include "Components/PCGSplineComponent.h"
#include "Data/PCGUtilsComponentData.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"
#include "OverrideGraphs.h"
#include "Algo/Transform.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "PCGGetPCGSplineDataElement"

UPCGGetPCGSplineDataSettings::UPCGGetPCGSplineDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
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

	const UPCGGetPCGSplineDataSettings* GetSettings = CastChecked<UPCGGetPCGSplineDataSettings>(Settings);

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
		if (!Comp || !ShouldProcessSplineComponent(Comp))
		{
			continue;
		}

		UPCGSplineData* SplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
		SplineData->Initialize(SplineComponent);
		
		if (SplineData->Metadata)
		{
			if (GetSettings->IsClosedAttributeName.IsNone())
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(
					LOCTEXT("EmptyIsClosedAttributeName",
						"Get PCG Spline Data could not write the closed-state metadata for component {0}: IsClosedAttributeName is None; expected a nonempty @Data Bool attribute name."),
					FText::FromString(GetNameSafe(Comp))));
			}
			else if (FPCGMetadataAttribute<bool>* IsClosedAttribute =
					SplineData->Metadata->FindOrCreateAttribute<bool>(
						FPCGAttributeIdentifier(GetSettings->IsClosedAttributeName, PCGMetadataDomainID::Data),
						SplineComponent->IsClosedLoop(),false,false,true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, SplineComponent->IsClosedLoop());
			}
			

			UPCGUtilPathDataLibrary::GetPathDataFromSettings(
				SplineData->Metadata, &GetSettings->PathSettings, &Comp->PathData);
			UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
				SplineData->Metadata, &GetSettings->ComponentSettings, Comp);
			WriteAdditionalSplineMetadata(Context, GetSettings, Comp, SplineData);
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = SplineData;
		Algo::Transform(SplineComponent->ComponentTags, TaggedData.Tags, NameTagsToStringTags);
		TaggedData.Tags.Append(ActorTags);
	}
}

bool FPCGGetPCGSplineDataElement::ShouldProcessSplineComponent(const UPCGSplineComponent* Component) const
{
	return Component != nullptr;
}

void FPCGGetPCGSplineDataElement::WriteAdditionalSplineMetadata(
	FPCGContext* Context,
	const UPCGGetPCGSplineDataSettings* Settings,
	const UPCGSplineComponent* Component,
	UPCGSplineData* SplineData) const
{
}

#undef LOCTEXT_NAMESPACE
