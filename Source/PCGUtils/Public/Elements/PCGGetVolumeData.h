// Copyright Max Harris

#pragma once

#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetVolumeData.generated.h"

UENUM(BlueprintType)
enum class EPCGVolumeDataOutputMode : uint8
{
	VolumeData UMETA(DisplayName="Volume Data"),
	DynMesh UMETA(DisplayName="DynMesh")
};

/** Retrieves PCG Volume Components from the selected actors, without provider interfaces. */
UCLASS(BlueprintType, ClassGroup=(Procedural), meta=(Keywords="volume brush"))
class PCGUTILS_API UPCGGetVolumeDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetVolumeDataSettings();

	/** One volume dataset or one DynMesh per accepted PCG Volume Component. Changes the output pin type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	EPCGVolumeDataOutputMode OutputMode = EPCGVolumeDataOutputMode::VolumeData;

	/** Express DynMesh vertices in the PCG target actor's local space; disable for world-space vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="OutputMode == EPCGVolumeDataOutputMode::DynMesh", EditConditionHides))
	bool bConvertWorldToActorLocal = true;

	/** Shared optional actor/component references and component relative transform, in the Data domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Component",
		meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume", meta=(PCG_Overridable))
	bool bOutputVolumeColor = false;

	/** RGBA vector in the Data domain (@Data). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume",
		meta=(PCG_Overridable, EditCondition="bOutputVolumeColor", EditConditionHides))
	FName VolumeColorAttributeName = TEXT("VolumeColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume", meta=(PCG_Overridable))
	bool bOutputVolumeDensity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume",
		meta=(PCG_Overridable, EditCondition="bOutputVolumeDensity", EditConditionHides))
	FName VolumeDensityAttributeName = TEXT("VolumeDensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume", meta=(PCG_Overridable))
	bool bOutputVolumeID = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Volume",
		meta=(PCG_Overridable, EditCondition="bOutputVolumeID", EditConditionHides))
	FName VolumeIDAttributeName = TEXT("VolumeID");

	virtual EPCGDataType GetDataFilter() const override
	{
		return OutputMode == EPCGVolumeDataOutputMode::DynMesh ? EPCGDataType::DynamicMesh : EPCGDataType::Volume;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetPCGVolumeData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetVolumeDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
