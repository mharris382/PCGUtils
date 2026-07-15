#include "Elements/PCGGetActorBakeSettings.h"

#include "Components/ActorComponent.h"
#include "Data/PCGBakeSettings.h"
#include "PCGParamData.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PCGBakeSettingsProvider.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGGetActorBakeSettingsElement"

namespace PCGGetActorBakeSettingsConstants
{
	const FName BakedAssetAttribute(TEXT("BakedAsset"));
	const FName SaveNameAttribute(TEXT("SaveName"));
	const FName SavePathAttribute(TEXT("SavePath"));
	const FName PreBakeGraphAttribute(TEXT("PreBakeGraph"));
	const FName PostBakeGraphAttribute(TEXT("PostBakeGraph"));
}

enum class EPCGActorBakeSettingsPreset : int32
{
	GetBakedSaveName,
	GetBakedSavePath,
	GetPreBakeOverride,
	GetPostBakeOverride,
	GetBakedAsset
};

UPCGGetActorBakeSettingsSettings::UPCGGetActorBakeSettingsSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
}

#if WITH_EDITOR
FText UPCGGetActorBakeSettingsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Actor Bake Settings");
}

FText UPCGGetActorBakeSettingsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Gets bake settings from an actor or its first bake-settings provider component.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGGetActorBakeSettingsSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(static_cast<int32>(EPCGActorBakeSettingsPreset::GetBakedSaveName), LOCTEXT("GetBakedSaveName", "GetBakedSaveName")),
		FPCGPreConfiguredSettingsInfo(static_cast<int32>(EPCGActorBakeSettingsPreset::GetBakedSavePath), LOCTEXT("GetBakedSavePath", "GetBakedSavePath")),
		FPCGPreConfiguredSettingsInfo(static_cast<int32>(EPCGActorBakeSettingsPreset::GetPreBakeOverride), LOCTEXT("GetPreBakeOverride", "GetPreBakeOverride")),
		FPCGPreConfiguredSettingsInfo(static_cast<int32>(EPCGActorBakeSettingsPreset::GetPostBakeOverride), LOCTEXT("GetPostBakeOverride", "GetPostBakeOverride")),
		FPCGPreConfiguredSettingsInfo(static_cast<int32>(EPCGActorBakeSettingsPreset::GetBakedAsset), LOCTEXT("GetBakedAsset", "GetBakedAsset"))
	};
}
#endif

void UPCGGetActorBakeSettingsSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	bExtractFullSoftObjectPath = false;
	bExtractSaveName = false;
	bExtractSavePath = false;
	bExtractPreBakeGraph = false;
	bExtractPostBakeGraph = false;

	switch (static_cast<EPCGActorBakeSettingsPreset>(PreconfiguredInfo.PreconfiguredIndex))
	{
	case EPCGActorBakeSettingsPreset::GetBakedSaveName: bExtractSaveName = true; break;
	case EPCGActorBakeSettingsPreset::GetBakedSavePath: bExtractSavePath = true; break;
	case EPCGActorBakeSettingsPreset::GetPreBakeOverride: bExtractPreBakeGraph = true; break;
	case EPCGActorBakeSettingsPreset::GetPostBakeOverride: bExtractPostBakeGraph = true; break;
	case EPCGActorBakeSettingsPreset::GetBakedAsset: bExtractFullSoftObjectPath = true; break;
	default: break;
	}
}

TArray<FPCGPinProperties> UPCGGetActorBakeSettingsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);
	return PinProperties;
}

FPCGElementPtr UPCGGetActorBakeSettingsSettings::CreateElement() const
{
	return MakeShared<FPCGGetActorBakeSettingsElement>();
}

void FPCGGetActorBakeSettingsElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	const UPCGGetActorBakeSettingsSettings* GetSettings = CastChecked<UPCGGetActorBakeSettingsSettings>(Settings);
	if (!IsValid(FoundActor))
	{
		return;
	}

	UObject* Provider = nullptr;
	if (FoundActor->GetClass()->ImplementsInterface(UPCGBakeSettingsProvider::StaticClass()))
	{
		Provider = FoundActor;
	}
	else
	{
		TInlineComponentArray<UActorComponent*, 8> Components;
		FoundActor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UPCGBakeSettingsProvider::StaticClass()))
			{
				Provider = Component;
				break;
			}
		}
	}

	if (!Provider)
	{
		PCGLog::LogErrorOnGraph(
			FText::Format(LOCTEXT("MissingProvider", "Actor '{0}' has no PCGBakeSettingsProvider."), FText::FromString(FoundActor->GetName())),
			Context);
		return;
	}

	const FPCGUtilsBakeSettings BakeSettings =
		IPCGBakeSettingsProvider::Execute_GetPCGBakeSettings(Provider);
	FString SaveName = BakeSettings.BakedAssetSaveName;
	if (!GetSettings->Label.IsEmpty())
	{
		SaveName += TEXT("_") + GetSettings->Label;
	}

	UPCGParamData* ParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	UPCGMetadata* Metadata = ParamData->MutableMetadata();
	const PCGMetadataEntryKey EntryKey = Metadata->AddEntry();
	const FPCGMetadataDomainID& Domain = PCGMetadataDomainID::Elements;

	if (GetSettings->bExtractFullSoftObjectPath)
	{
		FString PackagePath = BakeSettings.BakedAssetSavePath.Path;
		PackagePath.RemoveFromEnd(TEXT("/"));
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *SaveName, *SaveName);
		auto* Attribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			FPCGAttributeIdentifier(PCGGetActorBakeSettingsConstants::BakedAssetAttribute, Domain), FSoftObjectPath(), false, false, true);
		Attribute->SetValue(EntryKey, FSoftObjectPath(ObjectPath));
	}

	if (GetSettings->bExtractSaveName)
	{
		auto* Attribute = Metadata->FindOrCreateAttribute<FString>(
			FPCGAttributeIdentifier(PCGGetActorBakeSettingsConstants::SaveNameAttribute, Domain), FString(), false, false, true);
		Attribute->SetValue(EntryKey, SaveName);
	}

	if (GetSettings->bExtractSavePath)
	{
		auto* Attribute = Metadata->FindOrCreateAttribute<FString>(
			FPCGAttributeIdentifier(PCGGetActorBakeSettingsConstants::SavePathAttribute, Domain), FString(), false, false, true);
		Attribute->SetValue(EntryKey, BakeSettings.BakedAssetSavePath.Path);
	}

	if (GetSettings->bExtractPreBakeGraph)
	{
		auto* Attribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			FPCGAttributeIdentifier(PCGGetActorBakeSettingsConstants::PreBakeGraphAttribute, Domain), FSoftObjectPath(), false, false, true);
		Attribute->SetValue(EntryKey, BakeSettings.PreBakeGraph.GetOverrideGraphSoft());
	}

	if (GetSettings->bExtractPostBakeGraph)
	{
		auto* Attribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			FPCGAttributeIdentifier(PCGGetActorBakeSettingsConstants::PostBakeGraphAttribute, Domain), FSoftObjectPath(), false, false, true);
		Attribute->SetValue(EntryKey, BakeSettings.PostBakeGraph.GetOverrideGraphSoft());
	}

	FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
	TaggedData.Data = ParamData;
}

#undef LOCTEXT_NAMESPACE
