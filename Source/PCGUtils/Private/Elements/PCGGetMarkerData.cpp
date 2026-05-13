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
		if (!ShapeComp || !IsValid(ShapeComp))
		{
			continue;
		}
		FVector BoundsMax = ShapeComp->BoundsMax;
		FVector BoundsMin = ShapeComp->BoundsMin;
		UPCGPointData* PointData = NewObject<UPCGPointData>();
		TArray<FPCGPoint>& PCGPoints = PointData->GetMutablePoints();
		PCGPoints.Reserve(1);
		FPCGPoint& Point = PCGPoints.Emplace_GetRef();
		
		Point.Transform = ShapeComp->GetComponentTransform();
		Point.BoundsMin = ShapeComp->BoundsMin;
		Point.BoundsMax = ShapeComp->BoundsMax;
		Point.Density = ShapeComp->bSetDensity ? ShapeComp->Density : ShapeSettings->DefaultPointDensity;
		Point.Color = ShapeComp->bSetPointColor ? ShapeComp->PointColor : ShapeSettings->DefaultPointColor;
		
		if (UPCGMetadata* Meta = PointData->MutableMetadata())
		{
			if (ShapeSettings->bOutputMarkerPriority)
			{
				Meta->FindOrCreateAttribute<int32>(FPCGAttributeIdentifier(FName(TEXT("Priority")), PCGMetadataDomainID::Default), ShapeComp->Priority , false, false, false);
			}
			
			if (ShapeSettings->bOutputActorReference)
			{
				Meta->FindOrCreateAttribute<FSoftObjectPath>(
					FPCGAttributeIdentifier(PCGPointDataConstants::ActorReferenceAttribute, PCGMetadataDomainID::Default),
					FSoftObjectPath(FoundActor),
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/false,
					/*bOverwriteIfTypeMismatch=*/false);
			}

			
			if (ShapeSettings->bOutputComponentReference)
			{
				Meta->FindOrCreateAttribute<FSoftObjectPath>(
					FPCGAttributeIdentifier(FName("ComponentReference"), PCGMetadataDomainID::Default),
					FSoftObjectPath(ShapeComp),
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/false,
					/*bOverwriteIfTypeMismatch=*/false);
			}
			
			
			if (ShapeSettings->bOutputOverrideGraph)
			{
				Meta->FindOrCreateAttribute<FSoftObjectPath>(
					FPCGAttributeIdentifier(ShapeSettings->OverrideOutputGraphName, PCGMetadataDomainID::Default), 
					ShapeComp->PointOverrideGraph.bUseGraph ? ShapeComp->PointOverrideGraph.Graph : FSoftObjectPath() ,
					false, false, false);
			}
		}
		
		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(ShapeComp->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
