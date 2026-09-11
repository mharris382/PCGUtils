// Copyright Max Harris

#include "Elements/Conversion/PCGGetGeometryCollectionData.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetGeometryCollectionData"

namespace PCGGetGeometryCollectionDataPrivate
{
	const FName CollectionOutputPin = TEXT("GC");
}

UPCGGetGeometryCollectionDataSettings::UPCGGetGeometryCollectionDataSettings()
{
	// This node always operates on UGeometryCollectionComponent, so it specialises (and hides) the inherited
	// component selector's class rather than adding a second, conflicting one - matching how Get Spline Mesh
	// Data and Epic's own typed getters do it.
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;

	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = UGeometryCollectionComponent::StaticClass();
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetGeometryCollectionDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Get GC Data");
}

FText UPCGGetGeometryCollectionDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Finds Geometry Collection Components on the selected actors and pulls each one's collection into the "
		"graph as GC data - one data per component. Unlike GC | From Asset, this keeps where the component sits: "
		"its placement is re-expressed relative to the PCG target actor, so world-space PCG data such as a bounds "
		"selection or a point scatter lines up with the geometry. Read-only: the components, their assets and "
		"their materials are never modified, and each collection is deep-copied.");
}

FLinearColor UPCGGetGeometryCollectionDataSettings::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(PCGUtilsFracture::DomainColorHex));
}
#endif

TArray<FPCGPinProperties> UPCGGetGeometryCollectionDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGetGeometryCollectionDataPrivate::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true));
	return Pins;
}

FPCGElementPtr UPCGGetGeometryCollectionDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetGeometryCollectionDataElement>();
}

void FPCGGetGeometryCollectionDataElement::ProcessActor(
	FPCGContext* InContext, const UPCGDataFromActorSettings* InSettings, AActor* FoundActor) const
{
	check(InContext);
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);
	const UPCGGetGeometryCollectionDataSettings* Settings =
		CastChecked<UPCGGetGeometryCollectionDataSettings>(InSettings);

	if (!IsValid(FoundActor))
	{
		return;
	}

	TInlineComponentArray<UGeometryCollectionComponent*, 4> Components;
	FoundActor->GetComponents(Components);
	if (Components.IsEmpty())
	{
		// Not an error: the actor query is allowed to match actors that carry no collection.
		return;
	}

	// The space the rest of the graph is in. Resolved once per actor rather than per component, and only when
	// it is actually needed, so an unplaced import does not warn about a target actor it never uses.
	FTransform TargetActorToWorld = FTransform::Identity;
	bool bPlace = Settings->bConvertWorldToActorLocal;
	if (bPlace)
	{
		if (const AActor* TargetActor = InContext->GetTargetActor(nullptr))
		{
			TargetActorToWorld = TargetActor->GetActorTransform();
		}
		else
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
				"GC | Get GC Data could not resolve a PCG target actor, so each collection keeps its world "
				"placement instead of being expressed relative to one."), InContext);
		}
	}

	for (UGeometryCollectionComponent* Component : Components)
	{
		if (!IsValid(Component) || !Context->ComponentSelector.FilterComponent(Component))
		{
			continue;
		}

		if (Settings->bIgnorePCGGeneratedComponents &&
			Component->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
		{
			continue;
		}

		const UGeometryCollection* Asset = Component->GetRestCollection();
		const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> SourceCollection =
			Asset ? Asset->GetGeometryCollection() : nullptr;
		if (!SourceCollection.IsValid() ||
			SourceCollection->NumElements(FGeometryCollection::TransformGroup) == 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NoRestCollection",
					"GC | Get GC Data skipped Geometry Collection Component '{0}' on Actor '{1}': it has no "
					"collection, or the collection holds no bones."),
				FText::FromString(Component->GetName()),
				FText::FromString(FoundActor->GetActorNameOrLabel())), InContext);
			continue;
		}

		// Read-only extraction: the component's asset keeps its own collection, and the copy is what the graph
		// mutates. Same CopyTo reasoning as UPCGGeometryCollectionData::CreateMutableCopy.
		TSharedRef<FGeometryCollection> Collection = MakeShared<FGeometryCollection>();
		SourceCollection->CopyTo(&Collection.Get());

		TArray<FString> MissingAttributes;
		if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(*Collection, MissingAttributes))
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MalformedComponentCollection",
					"GC | Get GC Data skipped Geometry Collection Component '{0}' on Actor '{1}': its collection "
					"is missing the attribute(s) fracture requires ({2})."),
				FText::FromString(Component->GetName()),
				FText::FromString(FoundActor->GetActorNameOrLabel()),
				FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), InContext);
			continue;
		}

		// This is the whole point of the node. The component's geometry is authored in its own local space and
		// placed by its component transform; GC data is canonically in the PCG target actor's local space. So
		// the placement is carried across as a relative transform - keeping the collection exactly where it
		// looks like it is once the graph's own world/local conversions are applied - rather than dropped, which
		// would leave world-authored selections resolving against geometry at the origin.
		if (bPlace)
		{
			PCGUtilsGeometryCollectionHelpers::PlaceCollection(
				*Collection, Component->GetComponentTransform().GetRelativeTransform(TargetActorToWorld));
		}

		TArray<TObjectPtr<UMaterialInterface>> Materials;
		if (Settings->bExtractMaterials)
		{
			// The component's *effective* materials, so component-level overrides win over the asset's list.
			const int32 NumMaterials = Component->GetNumMaterials();
			Materials.Reserve(NumMaterials);
			for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
			{
				Materials.Add(Component->GetMaterial(MaterialIndex));
			}
		}

		// A new lineage per component: two components are two unrelated collections, and a bone selection
		// authored against one must not resolve against the other.
		FPCGUtilsGeometryCollectionPublishOptions PublishOptions;
		PublishOptions.bCompactHiddenGeometry = !Settings->bKeepHiddenGeometry;

		UPCGGeometryCollectionData* OutputData = PCGUtilsGeometryCollectionRevisionPublisher::PublishNewLineage(
			InContext, Collection, MoveTemp(Materials), PublishOptions);
		if (!OutputData)
		{
			continue;
		}

		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGGetGeometryCollectionDataPrivate::CollectionOutputPin;

		UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Get GC Data: '%s' on '%s' -> %s"),
			*Component->GetName(), *FoundActor->GetActorNameOrLabel(),
			*PCGUtilsGeometryCollectionHelpers::DescribeCollection(*Collection));
	}
}

#undef LOCTEXT_NAMESPACE
