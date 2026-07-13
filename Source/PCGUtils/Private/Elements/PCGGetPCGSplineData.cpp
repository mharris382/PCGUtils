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
		

		UPCGSplineData* SplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
		SplineData->Initialize(SplineComponent);
		
		
		UPCGSplineComponent* Comp = Cast<UPCGSplineComponent>(SplineComponent);
        if (!Comp)
        	continue;
		
		float Height = 0;
		FSoftObjectPath OverrideGraphPath = FSoftObjectPath();
		FLinearColor PathColor = FLinearColor::White;
		float PathDensity = 1.0f;
		int32 PathGroup = 0;
		
		if (Comp)
		{
			OverrideGraphPath= Comp->PreProcessSplineGraph.GetOverrideGraphSoft();
			Height = Comp->Height;
			PathGroup = Comp->GroupID;	
			PathColor = Comp->bSetPathColor ? Comp->PathColor : PathColor;
			PathDensity = Comp->PathDensity ? Comp->PathDensity : PathDensity;
		}
		
		if (SplineComponent && SplineData->Metadata)
		{
			if (FPCGMetadataAttribute<bool>* IsClosedAttribute =
					SplineData->Metadata->FindOrCreateAttribute<bool>(
						FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data),
						SplineComponent->IsClosedLoop(),false,false,true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, SplineComponent->IsClosedLoop());
			}
			
			if (GetSettings->bExtractHeight && !GetSettings->HeightAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<float>* HeightAttribute =
					SplineData->Metadata->FindOrCreateAttribute<float>(
						FPCGAttributeIdentifier(FName(*GetSettings->HeightAttributeName), PCGMetadataDomainID::Data),
						Height,false,false,true))
				{
					HeightAttribute->SetValue(PCGInvalidEntryKey,Height);
				}
			}

			if (GetSettings->bExtractPreProcessSplineGraph && !GetSettings->PreProcessSplineGraphAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
					SplineData->Metadata->FindOrCreateAttribute<FSoftObjectPath>(
						FPCGAttributeIdentifier(FName(*GetSettings->PreProcessSplineGraphAttributeName), PCGMetadataDomainID::Data),
						OverrideGraphPath,false,false,true))
				{
					GraphAttribute->SetValue(PCGInvalidEntryKey, OverrideGraphPath);
				}
			}
			
			if (GetSettings -> bExtractGroup && !GetSettings->GroupAttributeName.IsEmpty())
			{
				if (FPCGMetadataAttribute<int32>* GroupAttribute =
					SplineData->Metadata->FindOrCreateAttribute<int32>(
						FPCGAttributeIdentifier(FName(*GetSettings->GroupAttributeName), PCGMetadataDomainID::Data),
						PathGroup,false,false,true))
				{
					GroupAttribute->SetValue(PCGInvalidEntryKey, PathGroup);
				}
			}
			if (GetSettings->bExtractPathColorAttribute && !GetSettings->PathColorAttribute.IsEmpty())
			{
				FVector4 ColorV = FVector4(PathColor);
				if (FPCGMetadataAttribute<FVector4>* ColorAttribute =
					SplineData->Metadata->FindOrCreateAttribute<FVector4>(
						FPCGAttributeIdentifier(FName(*GetSettings->PathColorAttribute), PCGMetadataDomainID::Data),
						ColorV,false,false,true))
				{
					ColorAttribute->SetValue(PCGInvalidEntryKey, ColorV);
				}
			}
			if (GetSettings->bExtractPathDensityAttribute && !GetSettings->PathDensityAttribute.IsEmpty())
			{
				if (FPCGMetadataAttribute<float>* DensityAttribute =SplineData->Metadata->FindOrCreateAttribute<float>(
						FPCGAttributeIdentifier(FName(*GetSettings->PathDensityAttribute), PCGMetadataDomainID::Data),
						PathDensity,false,false,true))
				{
					DensityAttribute->SetValue(PCGInvalidEntryKey, PathDensity);
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
