// Copyright Max Harris

#include "Elements/AssetManagement/PCGGetActorBakeSaveParameters.h"

#include "Data/PCGBakeSettings.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGEdge.h"
#include "PCGNode.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "UObject/UnrealType.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetActorBakeSaveParameters"

namespace PCGActorBakeSaveParametersPrivate
{
	void ForwardUnchanged(FPCGContext* Context, const UPCGGetActorBakeSaveParametersSettings& Settings)
	{
		if (Settings.bUseInputData && Settings.bWriteToDataDomain)
		{
			for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
			{
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
			}
		}
	}

	bool ValidateOutputNames(FPCGContext* Context, const UPCGGetActorBakeSaveParametersSettings& Settings)
	{
		const TPair<const TCHAR*, FName> Names[] = {
			{TEXT("Output Asset Name Attribute"), Settings.OutputAssetNameAttribute},
			{TEXT("Output Asset Path Attribute"), Settings.OutputAssetPathAttribute},
			{TEXT("Output Asset Reference Attribute"), Settings.OutputAssetReferenceAttribute}};
		TSet<FName> Used;
		for (const auto& Name : Names)
		{
			if (Name.Value.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name.Value))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InvalidOutputName", "Get Actor Bake Save Parameters: {0} is '{1}'; expected a non-empty regular attribute name, without a domain prefix."), FText::FromString(Name.Key), FText::FromName(Name.Value)), Context);
				return false;
			}
			if (Used.Contains(Name.Value))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("DuplicateOutputName", "Get Actor Bake Save Parameters: {0} duplicates '{1}'; all three output attribute names must differ."), FText::FromString(Name.Key), FText::FromName(Name.Value)), Context);
				return false;
			}
			Used.Add(Name.Value);
		}
		if (Settings.bUseInputData && (Settings.AssetLabelAttributeName.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Settings.AssetLabelAttributeName)))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InvalidLabelName", "Get Actor Bake Save Parameters: Asset Label Attribute Name is '{0}'; expected a regular attribute name such as Label, without @Data. The node reads that name on @Data."), FText::FromName(Settings.AssetLabelAttributeName)), Context);
			return false;
		}
		return true;
	}

	const FPCGUtilsBakeSettings* ResolveActorSettings(FPCGContext* Context, const UPCGGetActorBakeSaveParametersSettings& Settings)
	{
		AActor* Actor = Settings.SourceActor.Get();
		if (Settings.bUseSelf)
		{
			const UPCGComponent* Original = UPCGBlueprintHelpers::GetOriginalComponent(*Context);
			Actor = Original ? Original->GetOwner() : Context->GetTypedExecutionTarget<AActor>();
		}
		if (!Actor)
		{
			PCGLog::LogErrorOnGraph(Settings.bUseSelf
				? LOCTEXT("MissingSelf", "Get Actor Bake Save Parameters: Self has no source actor; execute from an actor's PCG component or disable Use Self and select a loaded Source Actor.")
				: FText::Format(LOCTEXT("MissingActor", "Get Actor Bake Save Parameters: Source Actor '{0}' does not resolve to a loaded actor."), FText::FromString(Settings.SourceActor.ToString())), Context);
			return nullptr;
		}
		const FProperty* Property = Actor->GetClass()->FindPropertyByName(TEXT("BakeSettings"));
		if (!Property)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MissingSettings", "Get Actor Bake Save Parameters: actor '{0}' has no BakeSettings property."), FText::FromString(Actor->GetPathName())), Context);
			return nullptr;
		}
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		if (!Struct || Struct->Struct != FPCGUtilsBakeSettings::StaticStruct())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("WrongSettingsType", "Get Actor Bake Save Parameters: actor '{0}' has BakeSettings of type '{1}'; expected FPCGUtilsBakeSettings."), FText::FromString(Actor->GetPathName()), FText::FromString(Property->GetCPPType())), Context);
			return nullptr;
		}
		return Struct->ContainerPtrToValuePtr<FPCGUtilsBakeSettings>(Actor);
	}

	FString ReadLabel(FPCGContext* Context, const UPCGData* Data, FName Name, int32 Index)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* Domain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttributeBase* Attribute = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		if (!Attribute) return {};
		const PCGMetadataEntryKey Key = Domain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey;
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<FString>::Id)
		{
			return static_cast<const FPCGMetadataAttribute<FString>*>(Attribute)->GetValueFromItemKey(Key);
		}
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<FName>::Id)
		{
			const FName Label = static_cast<const FPCGMetadataAttribute<FName>*>(Attribute)->GetValueFromItemKey(Key);
			return Label.IsNone() ? FString() : Label.ToString();
		}
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("WrongLabelType", "Get Actor Bake Save Parameters: input {0}, @Data.{1} has type {2}; expected String or Name. Using the actor's base name without a label."), FText::AsNumber(Index), FText::FromName(Name), StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Attribute->GetTypeId())), Context);
		return {};
	}
}

#if WITH_EDITOR
FText UPCGGetActorBakeSaveParametersSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Param | Get Actor Bake Save Parameters");
}

FText UPCGGetActorBakeSaveParametersSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Reads Self's BakeSettings and optionally appends a label to the save name. Outputs AssetName (String), AssetPath (String) and Asset (soft object path), as an Attribute Set or on each input's @Data. No asset is saved or loaded.");
}

EPCGChangeType UPCGGetActorBakeSaveParametersSettings::GetChangeTypeForProperty(FPropertyChangedEvent& Event) const
{
	EPCGChangeType Change = Super::GetChangeTypeForProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UPCGGetActorBakeSaveParametersSettings, bUseInputData)
		|| Name == GET_MEMBER_NAME_CHECKED(UPCGGetActorBakeSaveParametersSettings, bWriteToDataDomain))
	{
		Change |= EPCGChangeType::Structural;
	}
	return Change;
}
#endif

TArray<FPCGPinProperties> UPCGGetActorBakeSaveParametersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (bUseInputData)
	{
		Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any, true, true,
			LOCTEXT("InTooltip", "One optional @Data label per dataset. Each input produces one output dataset.")).SetRequiredPin();
	}
	return Pins;
}

TArray<FPCGPinProperties> UPCGGetActorBakeSaveParametersSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, HasDynamicPins() ? EPCGDataType::Any : EPCGDataType::Param, true, bUseInputData)};
}

FPCGDataTypeIdentifier UPCGGetActorBakeSaveParametersSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (!InPin || !HasDynamicPins()
		|| InPin->Properties.Label != (InPin->IsOutputPin() ? PCGPinConstants::DefaultOutputLabel : PCGPinConstants::DefaultInputLabel))
	{
		return InPin ? Super::GetCurrentPinTypesID(InPin) : FPCGDataTypeIdentifier{EPCGDataType::Any};
	}
	// Bidirectional inference can visit this node again through a neighboring dynamic node or a cycle.
	static thread_local TSet<const UPCGGetActorBakeSaveParametersSettings*> Resolving;
	if (Resolving.Contains(this)) return EPCGDataType::Any;
	Resolving.Add(this);
	ON_SCOPE_EXIT { Resolving.Remove(this); };
	const FPCGDataTypeIdentifier InputTypes = GetTypeUnionIDOfIncidentEdges(PCGPinConstants::DefaultInputLabel);
	if (InputTypes.IsValid() && InputTypes != FPCGDataTypeIdentifier{EPCGDataType::Any}) return InputTypes;
	const UPCGNode* Node = Cast<UPCGNode>(GetOuter());
	const UPCGPin* Output = Node ? Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel) : nullptr;
	TArray<FPCGDataTypeIdentifier> Types;
	PCGSettingsHelpers::FPinTypeScopeHelper Scope;
	if (Output)
	{
		for (const UPCGEdge* Edge : Output->Edges)
		{
			const UPCGPin* Other = Edge ? Edge->GetOtherPin(Output) : nullptr;
			const FPCGDataTypeIdentifier Type = Scope.GetCurrentPinTypeID(Other);
			if (Type.IsValid() && Type != FPCGDataTypeIdentifier{EPCGDataType::Any}) Types.AddUnique(Type);
		}
	}
	return Types.IsEmpty() ? FPCGDataTypeIdentifier{EPCGDataType::Any} : FPCGDataTypeIdentifier::Construct(Types);
}

FPCGElementPtr UPCGGetActorBakeSaveParametersSettings::CreateElement() const
{
	return MakeShared<FPCGGetActorBakeSaveParametersElement>();
}

bool FPCGGetActorBakeSaveParametersElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	using namespace PCGActorBakeSaveParametersPrivate;
	const auto* Settings = Context->GetInputSettings<UPCGGetActorBakeSaveParametersSettings>();
	check(Settings);
	if (!ValidateOutputNames(Context, *Settings))
	{
		ForwardUnchanged(Context, *Settings);
		return true;
	}
	const FPCGUtilsBakeSettings* Bake = ResolveActorSettings(Context, *Settings);
	if (!Bake)
	{
		ForwardUnchanged(Context, *Settings);
		return true;
	}
	FString AssetPath = Bake->BakedAssetSavePath.Path.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	while (AssetPath.EndsWith(TEXT("/"))) AssetPath.LeftChopInline(1);
	if (Bake->BakedAssetSaveName.IsEmpty() || AssetPath.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(Bake->BakedAssetSaveName.IsEmpty()
			? LOCTEXT("EmptyBaseName", "Get Actor Bake Save Parameters: BakeSettings.BakedAssetSaveName is empty; expected an asset name.")
			: LOCTEXT("EmptyDirectory", "Get Actor Bake Save Parameters: BakeSettings.BakedAssetSavePath is empty; expected a long package folder such as /Game/Baked."), Context);
		ForwardUnchanged(Context, *Settings);
		return true;
	}
	if (!AssetPath.StartsWith(TEXT("/"))) AssetPath = TEXT("/") + AssetPath;
	TArray<FPCGTaggedData> Inputs;
	if (Settings->bUseInputData) Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	else Inputs.Emplace();
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const FPCGTaggedData& Input = Inputs[Index];
		if (Settings->bUseInputData && !Input.Data) continue;
		const FString Label = Settings->bUseInputData ? ReadLabel(Context, Input.Data, Settings->AssetLabelAttributeName, Index) : Settings->AssetLabel;
		const FString AssetName = Bake->BakedAssetSaveName + Label;
		const FString ReferenceString = FString::Printf(TEXT("%s/%s.%s"), *AssetPath, *AssetName, *AssetName);
		FText Reason;
		if (!FPackageName::IsValidObjectPath(ReferenceString, &Reason))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InvalidAssetPath", "Get Actor Bake Save Parameters: input {0} produces invalid asset reference '{1}': {2}"), FText::AsNumber(Index), FText::FromString(ReferenceString), Reason), Context);
			if (Settings->HasDynamicPins())
			{
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
			}
			continue;
		}
		const FSoftObjectPath Reference(ReferenceString);
		UPCGData* Data = Settings->HasDynamicPins() ? Input.Data->DuplicateData(Context, true) : FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		FPCGMetadataDomain* Domain = nullptr;
		if (Settings->HasDynamicPins())
		{
			Domain = PCGUtilsDataDomain::GetMutableDataDomain(Data);
			if (Domain) PCGUtilsDataDomain::CopyDataAttributes(Input.Data, *Domain);
		}
		else if (Data && Data->MutableMetadata())
		{
			Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		}
		if (Domain)
		{
			PCGUtilsDataDomain::WriteValue(*Domain, Settings->OutputAssetNameAttribute, AssetName);
			PCGUtilsDataDomain::WriteValue(*Domain, Settings->OutputAssetPathAttribute, AssetPath);
			PCGUtilsDataDomain::WriteValue(*Domain, Settings->OutputAssetReferenceAttribute, Reference);
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("UnsupportedDomain", "Get Actor Bake Save Parameters: input {0} ({1}) cannot hold @Data attributes; preserving the input unchanged."), FText::AsNumber(Index), FText::FromString(Input.Data ? Input.Data->GetClass()->GetName() : TEXT("null"))), Context);
			Data = const_cast<UPCGData*>(Input.Data.Get());
		}
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = Data;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
