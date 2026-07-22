// Fill out your copyright notice in the Description page of Project Settings.

#include "Elements/PCGGetMarkerData.h"

#include "Components/PCGMarkerComponent.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Data/PCGBasePointData.h"

#include "GameFramework/Actor.h"
#include "Algo/Transform.h"
#define LOCTEXT_NAMESPACE "PCGGetMarkerDataElement"

UPCGGetMarkerDataSettings::UPCGGetMarkerDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetMarkerDataSettings::GetDefaultNodeTitle() const
{
	return NSLOCTEXT("PCGGetMarkerDataElement", "NodeTitle", "Get Marker Data");
}

FText UPCGGetMarkerDataSettings::GetNodeTooltipText() const
{
	return NSLOCTEXT("PCGGetMarkerDataElement", "NodeTooltip",
		"Collects UPCGMarkerComponents as individual point data from actors and outputs them as PCG PointData. "
		"Optionally outputs actor and component references as well");
}
#endif

TArray<FPCGPinProperties> UPCGGetMarkerDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetMarkerDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetMarkerDataElement>();
}
void FPCGGetMarkerDataElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	const UPCGGetMarkerDataSettings* ShapeSettings = CastChecked<UPCGGetMarkerDataSettings>(Settings);
	check(ShapeSettings);
	
	if (!FoundActor || !IsValid(FoundActor))
	{
		return;
	}
	auto NameToString = [](const FName& N) { return N.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);
	
	TInlineComponentArray<UPCGMarkerComponent*, 4> ShapeComps;
	FoundActor->GetComponents(ShapeComps);
	for (UPCGMarkerComponent* ShapeComp : ShapeComps)
	{
		if (!IsValid(ShapeComp)
			|| !ShapeSettings->ComponentSelector.FilterComponent(ShapeComp)
			|| !ShouldFilterMarker(ShapeSettings, ShapeComp))
		{
			continue;
		}
		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		TArray<FPCGPoint>& PCGPoints = PointData->GetMutablePoints();
		PCGPoints.Reserve(1);
		FPCGPoint& Point = PCGPoints.Emplace_GetRef();
		
		Point.Transform = ShapeComp->GetComponentTransform();
		Point.BoundsMin = ShapeComp->BoundsMin;
		Point.BoundsMax = ShapeComp->BoundsMax;
		Point.Density = ShapeComp->PointData.GetPointDensity();
		Point.Steepness = ShapeComp->PointData.Steepness;
		Point.Color = ShapeComp->PointData.GetPointColor();
		
		if (UPCGMetadata* Meta = PointData->MutableMetadata())
		{
			UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
				Meta, &ShapeSettings->ComponentSettings, ShapeComp);
			

			const bool bUseElementsDomain =
				ShapeSettings->PointSettings.AttributeDomain == EPCGUtilsPointMetadataDomain::Elements;
			const FPCGMetadataDomainID& AttributeDomain = bUseElementsDomain
				? PCGMetadataDomainID::Elements
				: PCGMetadataDomainID::Data;
			const PCGMetadataEntryKey AttributeKey = bUseElementsDomain
				? Meta->AddEntry()
				: PCGInvalidEntryKey;

			if (bUseElementsDomain)
			{
				Point.MetadataEntry = AttributeKey;
			}

			UPCGUtilPathDataLibrary::GetPointDataFromSettings(
				Meta, &ShapeSettings->PointSettings, &ShapeComp->PointData, AttributeDomain, AttributeKey);
			
			AddMetadataFromMarker(Context, ShapeSettings, FoundActor, ShapeComp, Meta);
		}
		
		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(ShapeComp->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

void FPCGGetMarkerDataElement::AddMetadataFromMarker(
	FPCGContext* context,
	const UPCGGetMarkerDataSettings* settings,
	const AActor* actor,
	const UPCGMarkerComponent* marker,
	UPCGMetadata* mutableMetadata) const
{
}



#undef LOCTEXT_NAMESPACE
