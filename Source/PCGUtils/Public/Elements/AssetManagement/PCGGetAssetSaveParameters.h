#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGSettings.h"

#include "PCGGetAssetSaveParameters.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsAssetSaveParameterSource : uint8
{
	Constant UMETA(DisplayName = "Constant", ToolTip = "Derive save parameters from the soft object set on this node."),
	Attribute UMETA(DisplayName = "Attribute", ToolTip = "Derive save parameters from every soft object path in an input Attribute Set.")
};

/**
 * Derives the AssetName and AssetPath strings expected by PCG asset-saving nodes
 * from an existing asset reference. By default, the generated name includes an
 * underscore before the suffix so it cannot equal the source asset name.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|Asset Management")
class PCGUTILS_API UPCGGetAssetSaveParametersSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetAssetSaveParameters"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Selects between a one-off asset reference and a bulk Attribute Set input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	EPCGUtilsAssetSaveParameterSource Source = EPCGUtilsAssetSaveParameterSource::Constant;

	/** Existing asset used in Constant mode. The asset is referenced without being loaded. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings",
		meta = (PCG_Overridable, EditCondition = "Source == EPCGUtilsAssetSaveParameterSource::Constant", EditConditionHides))
	TSoftObjectPtr<UObject> Asset;

	/** FSoftObjectPath attribute read from the input Attribute Set in Attribute mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings",
		meta = (PCG_Overridable, EditCondition = "Source == EPCGUtilsAssetSaveParameterSource::Attribute", EditConditionHides))
	FName AssetAttribute = TEXT("AssetReference");

	/** Use the original asset name exactly, allowing a downstream save node to modify or replace the source asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable, DisplayName = "Use Actual Asset"))
	bool bUseActualAsset = false;

	/** Appended as "_{Suffix}" to the source asset name. An empty suffix still appends the underscore. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", AdvancedDisplay,
		meta = (PCG_Overridable, EditCondition = "!bUseActualAsset", EditConditionHides))
	FString Suffix = TEXT("Copy");
};

class PCGUTILS_API FPCGGetAssetSaveParametersElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
