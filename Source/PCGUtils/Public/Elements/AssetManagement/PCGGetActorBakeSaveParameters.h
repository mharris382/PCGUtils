// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGGetActorBakeSaveParameters.generated.h"

class AActor;

/** Reads the source actor's BakeSettings without saving or loading a baked asset. */
UCLASS(BlueprintType, ClassGroup=(Procedural), meta=(Keywords="Bake Settings Save Name Path Asset Label"))
class PCGUTILS_API UPCGGetActorBakeSaveParametersSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetActorBakeSaveParameters"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Params); }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual bool HasDynamicPins() const override { return bUseInputData && bWriteToDataDomain; }
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;

	/** Use the original PCG component's owner, including when executing on a partition component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source", meta=(PCG_Overridable))
	bool bUseSelf = true;

	/** Loaded source actor containing an FPCGUtilsBakeSettings property named BakeSettings. Never loads another level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source", meta=(PCG_Overridable, EditCondition="!bUseSelf", EditConditionHides))
	TSoftObjectPtr<AActor> SourceActor;

	/** Read an optional label from each input dataset. Disabled: no data input, one Attribute Set using Asset Label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	bool bUseInputData = false;

	/** Append this optional String suffix verbatim to the actor's base save name. Include an underscore here if wanted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="!bUseInputData", EditConditionHides))
	FString AssetLabel;

	/** Optional String or Name on each input's @Data domain, appended verbatim to the actor's base save name so one actor can bake multiple assets. Add it upstream with Add Attribute targeting @Data.Label (or @Data.<your name>). Missing/empty labels leave the base name unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bUseInputData", EditConditionHides))
	FName AssetLabelAttributeName = TEXT("Label");

	/** Output each input with the three attributes added to @Data. Disabled: one single-row Attribute Set per input, with regular attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition="bUseInputData", EditConditionHides))
	bool bWriteToDataDomain = false;

	/** Required String output containing the actor's BakedAssetSaveName plus the label. The output name is editable to match the consuming save node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName OutputAssetNameAttribute = TEXT("AssetName");

	/** Required String output containing the normalized asset folder (for example /Game/Baked). Written in the selected output domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName OutputAssetPathAttribute = TEXT("AssetPath");

	/** Required FSoftObjectPath output for the planned asset: /Folder/Name.Name. Does not require the baked asset to exist or load it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName OutputAssetReferenceAttribute = TEXT("Asset");

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILS_API FPCGGetActorBakeSaveParametersElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
	// Actor properties are external to the input CRC. Read them anew on each generation.
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
