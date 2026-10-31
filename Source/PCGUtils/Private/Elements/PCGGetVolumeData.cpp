// Copyright Max Harris

#include "Elements/PCGGetVolumeData.h"

#include "Algo/Transform.h"
#include "Components/BrushComponent.h"
#include "Components/PCGVolumeComponent.h"
#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGVolumeData.h"
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
	return LOCTEXT("Tooltip", "Collects PCG Volume Components on standard UE volume actors. Outputs the owner's exact "
		"volume data, or converts its brush to a DynMesh. Color, density, ID and component references are optional "
		"Data-domain attributes. DynMesh vertices default to PCG target actor local space.");
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

		AVolume* Volume = Component->GetVolume();
		UBrushComponent* Brush = IsValid(Volume) ? Volume->GetBrushComponent() : nullptr;
		if (!IsValid(Brush) || !Brush->Brush || !Volume->GetBounds().GetBox().IsValid)
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("MissingVolume",
				"PCG Volume Component '{0}' needs an owning UE volume actor with valid brush geometry."),
				FText::FromString(Component->GetName())), Context);
			continue;
		}

		UPCGSpatialData* ProducedData = nullptr;
		if (Settings->OutputMode == EPCGVolumeDataOutputMode::DynMesh)
		{
			UE::Geometry::FDynamicMesh3 Mesh;
			UE::Conversion::FVolumeToMeshOptions Options;
			Options.bInWorldSpace = true;
			UE::Conversion::BrushComponentToDynamicMesh(Brush, Mesh, Options);
			if (Mesh.TriangleCount() == 0)
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh",
					"Get PCG Volume Data: brush conversion produced an empty DynMesh and was skipped."), Context);
				continue;
			}

			if (Settings->bConvertWorldToActorLocal)
			{
				if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
				{
					// Apply inverse separately, so rotated non-uniform scale is not collapsed into a relative FTransform.
					UE::Geometry::MeshTransforms::ApplyTransformInverse(Mesh,
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
			UPCGVolumeData* VolumeData = FPCGContext::NewObject_AnyThread<UPCGVolumeData>(Context);
			VolumeData->Initialize(Volume);
			ProducedData = VolumeData;
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
