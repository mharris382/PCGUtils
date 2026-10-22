// Copyright Max Harris

#include "Elements/Creation/PCGGetStaticMeshBrushData.h"

#include "Algo/Transform.h"
#include "Components/StaticMeshBrushCombinerComponent.h"
#include "Components/StaticMeshBrushComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/MeshTransforms.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetStaticMeshBrushDataElement"

namespace PCGGetStaticMeshBrushData
{
	/** Copies a cached mesh, places it, and adds it to the output. The cache entry itself is never handed out. */
	void EmitBrushMesh(FPCGContext* Context, const FStaticMeshBrushCachedMesh& Cached, const FTransform& WorldPlacement,
		const AActor* ReferenceActor, TSet<FString>&& Tags)
	{
		const FTransform OutputTransform = ReferenceActor
			? WorldPlacement.GetRelativeTransform(ReferenceActor->GetActorTransform())
			: WorldPlacement;

		UE::Geometry::FDynamicMesh3 Mesh = Cached.Mesh;
		MeshTransforms::ApplyTransform(Mesh, UE::Geometry::FTransformSRT3d(OutputTransform), true);

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(Mesh), Cached.ResolveMaterials());

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Tags = MoveTemp(Tags);
	}

	void AppendNameTags(const TArray<FName>& Names, TSet<FString>& OutTags)
	{
		Algo::Transform(Names, OutTags, [](const FName& Name) { return Name.ToString(); });
	}
}

UPCGGetStaticMeshBrushDataSettings::UPCGGetStaticMeshBrushDataSettings()
{
	// Always reads Static Mesh Brush Components: fix (and hide) the inherited component selector's class, as
	// Get Spline Mesh Data does, rather than adding a second class setting.
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;

	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = UStaticMeshBrushComponent::StaticClass();
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetStaticMeshBrushDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Get Static Mesh Brush Data");
}

FText UPCGGetStaticMeshBrushDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Reads Static Mesh Brush Components from the selected actors as Dynamic Mesh data. The meshes are prepared "
		"and cached outside the graph (LOD, baked scale, simplification; self union when the actor has a Static Mesh "
		"Brush Combiner Component), so the graph only copies and places them. Combined meshes carry the Combined Tag.");
}
#endif

TArray<FPCGPinProperties> UPCGGetStaticMeshBrushDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
	return PinProperties;
}

FPCGElementPtr UPCGGetStaticMeshBrushDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetStaticMeshBrushDataElement>();
}

void FPCGGetStaticMeshBrushDataElement::ProcessActors(FPCGContext* InContext, const UPCGDataFromActorSettings* InSettings,
	const TArray<AActor*>& FoundActors, TArray<FPCGTaskId>& OutDynamicDependencies) const
{
	using namespace PCGGetStaticMeshBrushData;
	check(InContext);
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);
	const UPCGGetStaticMeshBrushDataSettings* Settings = CastChecked<UPCGGetStaticMeshBrushDataSettings>(InSettings);

	const AActor* ReferenceActor = nullptr;
	if (Settings->bConvertWorldToActorLocal)
	{
		ReferenceActor = InContext->GetTargetActor(nullptr);
		if (!ReferenceActor)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
				"Convert World To Actor Local is on, but the PCG target actor could not be resolved; output stays in world space."),
				InContext);
		}
	}

	auto LogWarnings = [InContext](const TArray<FText>& Warnings)
	{
		for (const FText& Warning : Warnings)
		{
			PCGLog::LogWarningOnGraph(Warning, InContext);
		}
	};

	for (AActor* Actor : FoundActors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		TSet<FString> ActorTags;
		AppendNameTags(Actor->Tags, ActorTags);

		UStaticMeshBrushCombinerComponent* Combiner = Actor->FindComponentByClass<UStaticMeshBrushCombinerComponent>();
		const bool bUseCombined = Settings->Source == EPCGUtilsStaticMeshBrushSource::Combined
			|| (Settings->Source == EPCGUtilsStaticMeshBrushSource::Automatic && Combiner);

		if (bUseCombined)
		{
			if (!Combiner)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoCombiner",
					"Source is Combined, but actor '{0}' has no Static Mesh Brush Combiner Component; its brushes were not read. "
					"Add a combiner to the actor, or set Source to Automatic or Individual."),
					FText::FromString(Actor->GetActorNameOrLabel())), InContext);
				continue;
			}

			TArray<FText> Warnings;
			const TSharedPtr<const FStaticMeshBrushCachedMesh> Combined = Combiner->GetCombinedMesh(Warnings);
			LogWarnings(Warnings);
			if (!Combined)
			{
				continue;
			}

			TSet<FString> Tags = ActorTags;
			AppendNameTags(Combiner->ComponentTags, Tags);
			if (!Settings->CombinedTag.IsNone())
			{
				Tags.Add(Settings->CombinedTag.ToString());
			}
			EmitBrushMesh(InContext, *Combined, Combiner->GetCombinedFrame(), ReferenceActor, MoveTemp(Tags));
			continue;
		}

		TInlineComponentArray<UStaticMeshBrushComponent*, 4> Brushes;
		Actor->GetComponents(Brushes);
		for (UStaticMeshBrushComponent* Brush : Brushes)
		{
			if (!IsValid(Brush) || !Context->ComponentSelector.FilterComponent(Brush))
			{
				continue;
			}
			if (Settings->bIgnorePCGGeneratedComponents && Brush->ComponentHasTag(PCGHelpers::DefaultPCGTag))
			{
				continue;
			}

			FText Error;
			const TSharedPtr<const FStaticMeshBrushCachedMesh> Processed = Brush->GetProcessedMesh(Error);
			if (!Processed)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("BrushSkipped",
					"Static Mesh Brush '{0}' on actor '{1}' was skipped: {2}."),
					FText::FromString(Brush->GetName()), FText::FromString(Actor->GetActorNameOrLabel()), Error), InContext);
				continue;
			}

			TSet<FString> Tags = ActorTags;
			AppendNameTags(Brush->ComponentTags, Tags);
			EmitBrushMesh(InContext, *Processed, Brush->GetBrushPlacement(), ReferenceActor, MoveTemp(Tags));
		}
	}
}

void FPCGGetStaticMeshBrushDataElement::ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const
{
	checkf(false, TEXT("This should never be called directly"));
}

void FPCGGetStaticMeshBrushDataElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const
{
	checkf(false, TEXT("This should never be called directly"));
}

#undef LOCTEXT_NAMESPACE
