#include "Elements/PCGGetPCGCacheData.h"

#include "PCGDataCacheComponent.h"
#include "PCGUtilsDataCacheHelpers.h"
#include "Data/PCGPointArrayData.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGModule.h"

#define LOCTEXT_NAMESPACE "PCGGetPCGCacheData"

UPCGGetPCGCacheDataSettings::UPCGGetPCGCacheDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetPCGCacheDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Get PCG Cache Data");
}

FText UPCGGetPCGCacheDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Outputs deterministic soft references to PCG cache data assets without loading them.");
}

EPCGChangeType UPCGGetPCGCacheDataSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGGetPCGCacheDataSettings, OutputType))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGGetPCGCacheDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel,
		OutputType == EPCGUtilsDataCacheReferenceOutput::PointData ? EPCGDataType::Point : EPCGDataType::Param);
	return Pins;
}

FPCGElementPtr UPCGGetPCGCacheDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetPCGCacheDataElement>();
}

void FPCGGetPCGCacheDataElement::ProcessActors(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* InSettings,
	const TArray<AActor*>& FoundActors) const
{
	const UPCGGetPCGCacheDataSettings* Settings = CastChecked<UPCGGetPCGCacheDataSettings>(InSettings);

#if !WITH_EDITOR
	if (Settings->TargetCache == EPCGUtilsDataCache::Offline)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("OfflineRuntime",
			"Get PCG Cache Data cannot expose Offline caches outside editor builds."), Context);
		return;
	}
#endif

	if (!Settings->bSuppressSelfCacheWarning && Settings->ActorSelector.ActorFilter == EPCGActorFilter::Self)
	{
		AActor* SourceActor = nullptr;
		if (Context->ExecutionSource.IsValid())
		{
			SourceActor = Context->ExecutionSource->GetExecutionState().GetTypedTarget<AActor>();
		}
		if (SourceActor)
		{
			TInlineComponentArray<UPCGComponent*> PCGComponents(SourceActor);
			if (PCGComponents.Num() <= 1)
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("SelfWarning",
					"Get PCG Cache Data is querying Self. Loading cache data saved by the same actor may create a confusing or circular workflow. This is valid when separate PCG components on the actor produce and consume the cache."), Context);
			}
		}
	}

	for (AActor* Actor : FoundActors)
	{
		ProcessActor(Context, Settings, Actor);
	}
}

void FPCGGetPCGCacheDataElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* InSettings,
	AActor* FoundActor) const
{
	if (!IsValid(FoundActor)) return;
	const UPCGGetPCGCacheDataSettings* Settings = CastChecked<UPCGGetPCGCacheDataSettings>(InSettings);

	bool bMultiple = false;
	UPCGDataCacheComponent* CacheComponent = PCGUtilsDataCacheHelpers::FindSingleCacheComponent(FoundActor, bMultiple);
	if (bMultiple)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("MultipleComponents",
			"Actor '{0}' has multiple PCG Data Cache components and was skipped."), FText::FromString(FoundActor->GetName())), Context);
		return;
	}
	if (!CacheComponent || !CacheComponent->UsesCache(Settings->TargetCache)) return;

	FSoftObjectPath CachePath;
	if (!CacheComponent->GetCacheAssetPath(Settings->TargetCache, CachePath) || !CachePath.IsValid())
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("InvalidPath",
			"Actor '{0}' has an invalid PCG cache asset path and was skipped."), FText::FromString(FoundActor->GetName())), Context);
		return;
	}

	if (Settings->OutputType == EPCGUtilsDataCacheReferenceOutput::AttributeSet)
	{
		UPCGParamData* ParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		UPCGMetadata* Metadata = ParamData->MutableMetadata();
		FPCGMetadataAttribute<FSoftObjectPath>* Attribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			PCGUtilsDataCache::CacheAssetPathAttribute, FSoftObjectPath(), false, false, true);
		const PCGMetadataEntryKey Entry = Metadata->AddEntry();
		Attribute->SetValue(Entry, CachePath);
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Data = ParamData;
	}
	else
	{
		UPCGPointArrayData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		PointData->SetNumPoints(1, true);
		PointData->GetTransformValueRange()[0] = FoundActor->GetActorTransform();
		PointData->GetDensityValueRange()[0] = 1.0f;
		PointData->GetBoundsMinValueRange()[0] = FVector(-50.0f);
		PointData->GetBoundsMaxValueRange()[0] = FVector(50.0f);
		PointData->GetSeedValueRange()[0] = static_cast<int32>(GetTypeHash(FoundActor->GetFName()));

		UPCGMetadata* Metadata = PointData->MutableMetadata();
		FPCGMetadataDomain* ElementsDomain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<FSoftObjectPath>* Attribute = ElementsDomain->FindOrCreateAttribute<FSoftObjectPath>(
			PCGUtilsDataCache::CacheAssetPathAttribute, FSoftObjectPath(), false, true);
		auto MetadataEntries = PointData->GetMetadataEntryValueRange();
		ElementsDomain->InitializeOnSet(MetadataEntries[0]);
		Attribute->SetValue(MetadataEntries[0], CachePath);
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Data = PointData;
	}
}

#undef LOCTEXT_NAMESPACE
