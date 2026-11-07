// Copyright Max Harris

#include "Elements/PCGGetVolumeData.h"

#include "Algo/Transform.h"
#include "Components/BrushComponent.h"
#include "Components/PCGVolumeComponent.h"
#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGVolumeData.h"
#include "Data/PCGVolumeShapeData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "GameFramework/Volume.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetVolumeData"

namespace PCGGetVolumeDataPrivate
{
	template<typename T>
	void WriteVolumeAttribute(FPCGContext* Context, UPCGMetadata* Metadata, bool bEnabled, FName Name, const T& Value)
	{
		if (!bEnabled)
		{
			return;
		}

		if (Name.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name))
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidAttributeName",
				"Get PCG Volume Data: an enabled output attribute has an empty or invalid name and was skipped."), Context);
			return;
		}

		FPCGMetadataAttribute<T>* Attribute = Metadata->FindOrCreateAttribute<T>(
			FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data), Value, false, false, true);
		if (Attribute)
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("AttributeConflict",
				"Get PCG Volume Data could not write attribute '{0}'. Check for conflicting attribute names or types."),
				FText::FromName(Name)), Context);
		}
	}

	/** Logs and returns false for the first shape size that cannot enclose anything, naming it and its value. */
	bool ValidateShape(FPCGContext* Context, const UPCGVolumeComponent* Component)
	{
		const FText Name = FText::FromString(Component->GetName());
		const FText ShapeName = StaticEnum<EPCGVolumeShape>()->GetDisplayNameTextByValue(static_cast<int64>(Component->Shape));
		auto Fail = [&](const TCHAR* Property, double Value)
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("BadShapeSize",
				"Get PCG Volume Data: PCG Volume Component '{0}' ({1}): {2} is {3}; it must be greater than 0. Skipped."),
				Name, ShapeName, FText::FromString(Property), FText::AsNumber(Value)), Context);
			return false;
		};
		switch (Component->Shape)
		{
		case EPCGVolumeShape::Box:
			if (Component->BoxExtent.X <= 0.0) { return Fail(TEXT("Box Extent X"), Component->BoxExtent.X); }
			if (Component->BoxExtent.Y <= 0.0) { return Fail(TEXT("Box Extent Y"), Component->BoxExtent.Y); }
			if (Component->BoxExtent.Z <= 0.0) { return Fail(TEXT("Box Extent Z"), Component->BoxExtent.Z); }
			return true;
		case EPCGVolumeShape::Sphere:
			return Component->Radius > 0.0 || Fail(TEXT("Radius"), Component->Radius);
		case EPCGVolumeShape::Cylinder:
		case EPCGVolumeShape::Cone:
		case EPCGVolumeShape::Capsule:
			if (Component->Radius <= 0.0) { return Fail(TEXT("Radius"), Component->Radius); }
			if (Component->HalfHeight <= 0.0) { return Fail(TEXT("Half Height"), Component->HalfHeight); }
			return true;
		default:
			return true;
		}
	}
}

UPCGGetVolumeDataSettings::UPCGGetVolumeDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = UPCGVolumeComponent::StaticClass();
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetVolumeDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Get PCG Volume Data");
}

FText UPCGGetVolumeDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Collects PCG Volume Components from the selected actors. Outputs each component's "
		"shape (box, sphere, cylinder, cone or capsule) as exact volume data, or as a DynMesh; a component set to "
		"Owner Volume gives its Volume actor's brush instead. Color, density, ID and component references are "
		"optional Data-domain attributes. DynMesh vertices default to PCG target actor local space.");
}

EPCGChangeType UPCGGetVolumeDataSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGGetVolumeDataSettings, OutputMode))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGGetVolumeDataSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(OutputMode == EPCGVolumeDataOutputMode::DynMesh ? TEXT("DynMesh") : TEXT("Volume"), GetDataFilter())};
}

FPCGElementPtr UPCGGetVolumeDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetVolumeDataElement>();
}

void FPCGGetVolumeDataElement::ProcessActor(FPCGContext* InContext,
	const UPCGDataFromActorSettings* InSettings, AActor* FoundActor) const
{
	check(InContext && InSettings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);
	const UPCGGetVolumeDataSettings* Settings = CastChecked<UPCGGetVolumeDataSettings>(InSettings);
	TInlineComponentArray<UPCGVolumeComponent*, 4> Components;
	FoundActor->GetComponents(Components);

	const auto VolumeTagToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, VolumeTagToString);

	for (UPCGVolumeComponent* Component : Components)
	{
		// Use the resolved selector: its ComponentList includes references supplied on the inherited input pin.
		if (!IsValid(Component) || !Context->ComponentSelector.FilterComponent(Component)
			|| (Settings->bIgnorePCGGeneratedComponents && Component->ComponentHasTag(PCGHelpers::DefaultPCGTag)))
		{
			continue;
		}

		const bool bOwnerVolume = Component->Shape == EPCGVolumeShape::OwnerVolume;
		AVolume* Volume = nullptr;
		UBrushComponent* Brush = nullptr;
		if (bOwnerVolume)
		{
			Volume = Component->GetVolume();
			if (!IsValid(Volume))
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("OwnerNotVolume",
					"Get PCG Volume Data: PCG Volume Component '{0}' has Shape = Owner Volume, but its owner '{1}' is not a Volume actor. Skipped."),
					FText::FromString(Component->GetName()), FText::FromString(FoundActor->GetActorNameOrLabel())), Context);
				continue;
			}
			Brush = Volume->GetBrushComponent();
			if (!IsValid(Brush) || !Brush->Brush)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("VolumeNoBrush",
					"Get PCG Volume Data: PCG Volume Component '{0}' has Shape = Owner Volume, but Volume actor '{1}' has no brush geometry. Skipped."),
					FText::FromString(Component->GetName()), FText::FromString(Volume->GetActorNameOrLabel())), Context);
				continue;
			}
			if (!Volume->GetBounds().GetBox().IsValid)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("VolumeNoBounds",
					"Get PCG Volume Data: PCG Volume Component '{0}' has Shape = Owner Volume, but Volume actor '{1}' has empty bounds. Skipped."),
					FText::FromString(Component->GetName()), FText::FromString(Volume->GetActorNameOrLabel())), Context);
				continue;
			}
		}
		else if (!PCGGetVolumeDataPrivate::ValidateShape(Context, Component))
		{
			continue;
		}

		UPCGSpatialData* ProducedData = nullptr;
		if (Settings->OutputMode == EPCGVolumeDataOutputMode::DynMesh)
		{
			UE::Geometry::FDynamicMesh3 Mesh;
			if (bOwnerVolume)
			{
				UE::Conversion::FVolumeToMeshOptions Options;
				Options.bInWorldSpace = true;
				UE::Conversion::BrushComponentToDynamicMesh(Brush, Mesh, Options);
			}
			else
			{
				Component->GetShapeDesc().BuildMesh(Mesh, /*bWorldSpace=*/true);
			}
			if (Mesh.TriangleCount() == 0)
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh",
					"Get PCG Volume Data: the volume produced an empty DynMesh and was skipped."), Context);
				continue;
			}

			if (Settings->bConvertWorldToActorLocal)
			{
				if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
				{
					// Apply inverse separately, so rotated non-uniform scale is not collapsed into a relative FTransform.
					// UE 5.8: MeshTransforms is a top-level namespace in GeometryCore, not under UE::Geometry.
					MeshTransforms::ApplyTransformInverse(Mesh,
						UE::Geometry::FTransformSRT3d(TargetActor->GetActorTransform()), true);
				}
				else
				{
					PCGLog::LogWarningOnGraph(LOCTEXT("MissingTarget",
						"Get PCG Volume Data could not resolve a PCG target actor; DynMesh vertices remain in world space."), Context);
				}
			}

			UPCGDynamicMeshData* MeshData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
			MeshData->Initialize(MoveTemp(Mesh));
			ProducedData = MeshData;
		}
		else
		{
			if (bOwnerVolume)
			{
				UPCGVolumeData* VolumeData = FPCGContext::NewObject_AnyThread<UPCGVolumeData>(Context);
				VolumeData->Initialize(Volume);
				ProducedData = VolumeData;
			}
			else
			{
				UPCGVolumeShapeData* VolumeData = FPCGContext::NewObject_AnyThread<UPCGVolumeShapeData>(Context);
				VolumeData->Initialize(Component->GetShapeDesc());
				ProducedData = VolumeData;
			}
		}

		UPCGMetadata* Metadata = ProducedData->MutableMetadata();
		UPCGUtilPathDataLibrary::GetComponentDataFromSettings(Metadata, &Settings->ComponentSettings, Component);
		// Also warn for enabled shared outputs whose names the shared writer would silently skip.
		const auto WarnVolumeComponentAttributeName = [Context](bool bEnabled, const FString& Name)
		{
			if (bEnabled && (FName(*Name).IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name)))
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("InvalidComponentAttributeName",
					"Get PCG Volume Data: an enabled component attribute has an empty or invalid name and was skipped."), Context);
			}
		};
		WarnVolumeComponentAttributeName(Settings->ComponentSettings.bOutputActorReference,
			Settings->ComponentSettings.ActorReferenceAttributeName);
		WarnVolumeComponentAttributeName(Settings->ComponentSettings.bOutputComponentReference,
			Settings->ComponentSettings.ComponentReferenceAttributeName);
		WarnVolumeComponentAttributeName(Settings->ComponentSettings.bOutputComponentTransform,
			Settings->ComponentSettings.RelativeTransformAttributeName);

		PCGGetVolumeDataPrivate::WriteVolumeAttribute(Context, Metadata, Settings->bOutputVolumeColor,
			Settings->VolumeColorAttributeName, FVector4(Component->VolumeData.VolumeColor));
		PCGGetVolumeDataPrivate::WriteVolumeAttribute(Context, Metadata, Settings->bOutputVolumeDensity,
			Settings->VolumeDensityAttributeName, Component->VolumeData.VolumeDensity);
		PCGGetVolumeDataPrivate::WriteVolumeAttribute(Context, Metadata, Settings->bOutputVolumeID,
			Settings->VolumeIDAttributeName, Component->VolumeData.VolumeID);

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = ProducedData;
		TaggedData.Pin = Settings->OutputMode == EPCGVolumeDataOutputMode::DynMesh ? TEXT("DynMesh") : TEXT("Volume");
		Algo::Transform(Component->ComponentTags, TaggedData.Tags, VolumeTagToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
