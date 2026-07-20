#include "Elements/PCGGetPathData.h"

#include "PCGUtilsHelpers.h"
#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Data/PCGPathPoint.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGBasePointData.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PCGPathProvider.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetPathDataElement"

UPCGGetPathDataSettings::UPCGGetPathDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetPathDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get PCG Path Data");
}

FText UPCGGetPathDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Collects point paths from actors and actor components implementing PCGPathProvider.");
}
#endif

TArray<FPCGPinProperties> UPCGGetPathDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetPathDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetPathElement>();
}

void FPCGGetPathElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	if (!IsValid(FoundActor)) return;

	const UPCGGetPathDataSettings* GetSettings = CastChecked<UPCGGetPathDataSettings>(Settings);
	auto HasPathProvider = [](const AActor* Actor)
	{
		if (!IsValid(Actor)) return false;
		if (Actor->GetClass()->ImplementsInterface(UPCGPathProvider::StaticClass())) return true;
		TInlineComponentArray<UActorComponent*, 8> CandidateComponents;
		Actor->GetComponents(CandidateComponents);
		return CandidateComponents.ContainsByPredicate([](const UActorComponent* Component)
		{
			return IsValid(Component) && Component->GetClass()->ImplementsInterface(UPCGPathProvider::StaticClass());
		});
	};

	AActor* ProviderActor = FoundActor;
	const bool bSelfOrOriginalSelection = GetSettings->ActorSelector.ActorFilter == EPCGActorFilter::Self
		|| GetSettings->ActorSelector.ActorFilter == EPCGActorFilter::Original;
	if (bSelfOrOriginalSelection && !HasPathProvider(ProviderActor))
	{
		const IPCGGraphExecutionSource* ExecutionSource = Context->ExecutionSource.Get();
		const IPCGGraphExecutionSource* OriginalSource = ExecutionSource
			? ExecutionSource->GetExecutionState().GetOriginalSource()
			: nullptr;
		AActor* OriginalActor = OriginalSource
			? OriginalSource->GetExecutionState().GetTypedTarget<AActor>()
			: nullptr;
		if (IsValid(OriginalActor) && HasPathProvider(OriginalActor)) ProviderActor = OriginalActor;
	}

	const auto NameToString = [](const FName& Name) { return Name.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(ProviderActor->Tags, ActorTags, NameToString);

	auto ProcessProvider = [&](const UObject* Provider, USceneComponent* SceneComponent,
		const FTransform& LocalToWorld, const TArray<FName>* ProviderTags)
	{
		if (!IsValid(Provider) || !Provider->GetClass()->ImplementsInterface(UPCGPathProvider::StaticClass())) return;
		if (!ShouldProcessPathProvider(Context, GetSettings, Provider)) return;

		bool bIsLocalSpace = false;
		bool bIsLinearPath = true;
		TArray<FPCGPathPoint> ProviderPoints = IPCGPathProvider::Execute_GetPCGPathPoints(
			Provider, bIsLocalSpace, bIsLinearPath);
		if (ProviderPoints.IsEmpty()) return;

		if (bIsLocalSpace)
		{
			for (FPCGPathPoint& Point : ProviderPoints)
			{
				Point.Transform = Point.Transform * LocalToWorld;
				Point.ArriveTangent = LocalToWorld.TransformVector(Point.ArriveTangent);
				Point.LeaveTangent = LocalToWorld.TransformVector(Point.LeaveTangent);
			}
		}
		
		const FPathComponentData PathData = IPCGPathProvider::Execute_GetPathData(Provider);
		const bool bIsClosed = IPCGPathProvider::Execute_GetIsClosedLoop(Provider);
		
		UPCGPointArrayData* PointData = UPCGUtilsHelpers::CreatePointArrayDataFromPathPoints(Context, ProviderPoints);
		if (!PointData) return;

		UPCGMetadata* Metadata = PointData->MutableMetadata();
		if (Metadata)
		{
			if (!bIsLinearPath)
			{
				FPCGMetadataDomain* ElementsDomain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
				FPCGMetadataAttribute<FVector>* ArriveTangentAttribute = ElementsDomain
					? ElementsDomain->FindOrCreateAttribute<FVector>(FName("ArriveTangent"), FVector::ZeroVector, true, true)
					: nullptr;
				FPCGMetadataAttribute<FVector>* LeaveTangentAttribute = ElementsDomain
					? ElementsDomain->FindOrCreateAttribute<FVector>(FName("LeaveTangent"), FVector::ZeroVector, true, true)
					: nullptr;
				auto MetadataEntries = PointData->GetMetadataEntryValueRange();
				for (int32 Index = 0; Index < ProviderPoints.Num(); ++Index)
				{
					ElementsDomain->InitializeOnSet(MetadataEntries[Index]);
					if (ArriveTangentAttribute)
					{
						ArriveTangentAttribute->SetValue(MetadataEntries[Index], ProviderPoints[Index].ArriveTangent);
					}
					if (LeaveTangentAttribute)
					{
						LeaveTangentAttribute->SetValue(MetadataEntries[Index], ProviderPoints[Index].LeaveTangent);
					}
				}
			}

			if (FPCGMetadataAttribute<bool>* IsClosedAttribute = Metadata->FindOrCreateAttribute<bool>(
				FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data),
				bIsClosed, false, false, true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, bIsClosed);
			}

			if (SceneComponent)
			{
				UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
					Metadata, &GetSettings->ComponentSettings, SceneComponent);
			}
			UPCGUtilPathDataLibrary::GetPathDataFromSettings(
				Metadata, &GetSettings->PathSettings, &PathData);
		}

		TSet<FString> AdditionalTags;
		ProcessPathComponent(Context, GetSettings, ProviderActor, Provider, PointData, Metadata, AdditionalTags);

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		if (ProviderTags) Algo::Transform(*ProviderTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
		TaggedData.Tags.Append(AdditionalTags);
	};

	// Actors can provide paths directly. Their local points are relative to the actor transform.
	ProcessProvider(ProviderActor, nullptr, ProviderActor->GetActorTransform(), nullptr);

	TInlineComponentArray<UActorComponent*, 8> Components;
	ProviderActor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component)
			|| !Settings->ComponentSelector.FilterComponent(Component)
			|| !Component->GetClass()->ImplementsInterface(UPCGPathProvider::StaticClass()))
		{
			continue;
		}

		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		const FTransform LocalToWorld = SceneComponent
			? SceneComponent->GetComponentTransform()
			: ProviderActor->GetActorTransform();
		ProcessProvider(Component, SceneComponent, LocalToWorld, &Component->ComponentTags);
	}
}

void FPCGGetPathElement::ProcessPathComponent(FPCGContext* Context, const UPCGGetPathDataSettings* Settings,
                                              const AActor* Actor, const UObject* PathProvider, UPCGBasePointData* PointData,
                                              UPCGMetadata* MutableMetadata, TSet<FString>& OutTags) const
{
}


#undef LOCTEXT_NAMESPACE
