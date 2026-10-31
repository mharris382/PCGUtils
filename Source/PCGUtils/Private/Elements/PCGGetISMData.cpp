// Copyright Max Harris

#include "Elements/PCGGetISMData.h"

#include "Algo/Transform.h"
#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/PCGPointArrayData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsAttributeTargets.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGetISMData)

#define LOCTEXT_NAMESPACE "PCGGetISMData"

UPCGGetISMDataSettings::UPCGGetISMDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	// Reading back what PCG spawned is a main use; debug components have their own narrower filter.
	bIgnorePCGGeneratedComponents = false;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetISMDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get ISM Data");
}

FText UPCGGetISMDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Reads instanced static mesh components (ISM and HISM) on the selected actors back into PCG. Each component "
		"becomes one point data set with one world-space point per instance, tagged with the component's and actor's tags. "
		"The mesh and material are written under names you choose; prefix a name with '@Data.' to write it once per data set.");
}
#endif

TArray<FPCGPinProperties> UPCGGetISMDataSettings::OutputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("OutTip", "One point data set per instanced static mesh component, one point per instance.");
#endif
	return {Pin};
}

FPCGElementPtr UPCGGetISMDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetISMDataElement>();
}

UPCGBasePointData* FPCGGetISMDataElement::MakeInstancePoints(FPCGContext* Context, const UPCGGetISMDataSettings& Settings,
	const UInstancedStaticMeshComponent& Component)
{
	const int32 InstanceCount = Component.GetInstanceCount();
	if (InstanceCount == 0)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = Component.GetStaticMesh();
	UPCGPointArrayData* Points = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
	Points->SetNumPoints(InstanceCount);
	Points->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed
		| EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

	const bool bMeshBounds = Settings.bBoundsFromMesh && Mesh;
	const FBox MeshBounds = bMeshBounds ? Mesh->GetBoundingBox() : FBox(FVector(-1.0), FVector(1.0));

	auto Transforms = Points->GetTransformValueRange();
	auto Seeds = Points->GetSeedValueRange();
	auto Mins = Points->GetBoundsMinValueRange();
	auto Maxs = Points->GetBoundsMaxValueRange();
	ParallelFor(InstanceCount, [&](int32 Index)
	{
		FTransform Transform;
		Component.GetInstanceTransform(Index, Transform, /*bWorldSpace=*/true);
		Transforms[Index] = Transform;
		Seeds[Index] = PCGHelpers::ComputeSeedFromPosition(Transform.GetLocation());
		Mins[Index] = MeshBounds.Min;
		Maxs[Index] = MeshBounds.Max;
	});

	const FString ComponentLabel = FString::Printf(TEXT("'%s' on '%s'"), *Component.GetName(),
		Component.GetOwner() ? *Component.GetOwner()->GetActorNameOrLabel() : TEXT("no actor"));

	if (Settings.bOutputMesh)
	{
		PCGUtilsAttributeTargets::FResolvedTarget Target;
		FText Error;
		if (Settings.MeshAttributeName.IsNone())
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MeshNameNone", "Get ISM Data: Mesh Attribute Name is None, so the mesh was not written. Name it or turn the output off."), Context);
		}
		else if (!PCGUtilsAttributeTargets::Resolve(*Points, Settings.MeshAttributeName, TEXT("Mesh Attribute Name"), Target, Error))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MeshNameBad", "Get ISM Data: {0}"), Error), Context);
		}
		else
		{
			PCGUtilsAttributeTargets::WriteConstant<FSoftObjectPath>(Target, FSoftObjectPath(Mesh));
		}
	}

	if (Settings.bOutputMaterial)
	{
		PCGUtilsAttributeTargets::FResolvedTarget Target;
		FText Error;
		if (Settings.MaterialAttributeName.IsNone())
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MaterialNameNone", "Get ISM Data: Material Attribute Name is None, so the material was not written. Name it or turn the output off."), Context);
		}
		else if (Settings.MaterialSlot < 0 || Settings.MaterialSlot >= FMath::Max(1, Component.GetNumMaterials()))
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("SlotRange",
				"Get ISM Data: Material Slot is {0}, but component {1} has {2} material slot(s) (valid: 0 to {3}). The material attribute holds an empty path."),
				FText::AsNumber(Settings.MaterialSlot), FText::FromString(ComponentLabel),
				FText::AsNumber(Component.GetNumMaterials()), FText::AsNumber(Component.GetNumMaterials() - 1)), Context);
			if (PCGUtilsAttributeTargets::Resolve(*Points, Settings.MaterialAttributeName, TEXT("Material Attribute Name"), Target, Error))
			{
				PCGUtilsAttributeTargets::WriteConstant<FSoftObjectPath>(Target, FSoftObjectPath());
			}
		}
		else if (!PCGUtilsAttributeTargets::Resolve(*Points, Settings.MaterialAttributeName, TEXT("Material Attribute Name"), Target, Error))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MaterialNameBad", "Get ISM Data: {0}"), Error), Context);
		}
		else
		{
			PCGUtilsAttributeTargets::WriteConstant<FSoftObjectPath>(Target, FSoftObjectPath(Component.GetMaterial(Settings.MaterialSlot)));
		}
	}

	return Points;
}

void FPCGGetISMDataElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* InSettings, AActor* FoundActor) const
{
	check(Context && InSettings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	const UPCGGetISMDataSettings* Settings = CastChecked<UPCGGetISMDataSettings>(InSettings);
	const FPCGDataFromActorContext* ActorContext = static_cast<const FPCGDataFromActorContext*>(Context);

	TInlineComponentArray<UInstancedStaticMeshComponent*, 4> Components;
	FoundActor->GetComponents(Components);
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		if (!IsValid(Component)
			|| !ActorContext->ComponentSelector.FilterComponent(Component)
			|| (Settings->bSkipPCGDebugComponents && Component->ComponentHasTag(PCGHelpers::DefaultPCGDebugTag))
			|| (Settings->bIgnorePCGGeneratedComponents && Component->ComponentHasTag(PCGHelpers::DefaultPCGTag)))
		{
			continue;
		}

		UPCGBasePointData* Points = MakeInstancePoints(Context, *Settings, *Component);
		if (!Points)
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = Points;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Algo::Transform(Component->ComponentTags, Output.Tags, [](FName Tag) { return Tag.ToString(); });
		Algo::Transform(FoundActor->Tags, Output.Tags, [](FName Tag) { return Tag.ToString(); });
	}
}

#undef LOCTEXT_NAMESPACE
