#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGSettings.h"

#include "PCGGetAssetSaveParameters.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsAssetSaveParameterSource : uint8
{
	Constant UMETA(DisplayName = "Constant", ToolTip = "Derive save parameters from the soft object set on this node."),
	Attribute UMETA(DisplayName = "Attribute", ToolTip = "Derive save parameters from every soft object path in an input Attribute Set."),
	InputData UMETA(DisplayName = "Input Data", ToolTip = "Derive save parameters for each input data from the asset it names, and write them to that data's @Data. One node names a whole set of meshes for DynMesh | Save Static Mesh.")
};

/**
 * Derives the AssetName and AssetPath strings expected by PCG asset-saving nodes
 * from an existing asset reference. By default, the generated name includes an
 * underscore before the suffix so it cannot equal the source asset name.
 *
 * Constant and Attribute modes output an Attribute Set, one entry per asset. Input Data mode outputs its inputs,
 * each with the two values added to its @Data, so many meshes can be saved by one DynMesh | Save Static Mesh.
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
	virtual bool HasDynamicPins() const override { return Source == EPCGUtilsAssetSaveParameterSource::InputData; }
	/** In Input Data mode the output carries the inputs, so it takes their type. */
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;

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

	/**
	 * Input Data mode: the attribute naming each input's asset, a soft object path or a string. Required on every
	 * input, and it must hold one value per input: a @Data attribute, or a domain with a single element. An input
	 * without it is not output.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings",
		meta = (PCG_Overridable, DisplayName = "Asset Attribute", EditCondition = "Source == EPCGUtilsAssetSaveParameterSource::InputData", EditConditionHides))
	FPCGAttributePropertyInputSelector InputAssetAttribute;

	/** Use the original asset name exactly, allowing a downstream save node to modify or replace the source asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable, DisplayName = "Use Actual Asset"))
	bool bUseActualAsset = false;

	/** Appended as "_{Suffix}" to the source asset name. An empty suffix still appends the underscore. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", AdvancedDisplay,
		meta = (PCG_Overridable, EditCondition = "!bUseActualAsset", EditConditionHides))
	FString Suffix = TEXT("Copy");

	/**
	 * String attribute that receives the asset name: an Attribute Set attribute in Constant and Attribute modes, a
	 * @Data attribute in Input Data mode. Always written, it is what the node is for. Default AssetName, the name
	 * the save nodes read.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output", meta = (PCG_Overridable))
	FName AssetNameAttributeName = TEXT("AssetName");

	/** String attribute that receives the asset's folder, such as /Game/Meshes. Written like Asset Name Attribute Name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output", meta = (PCG_Overridable))
	FName AssetPathAttributeName = TEXT("AssetPath");
};

class PCGUTILS_API FPCGGetAssetSaveParametersElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
