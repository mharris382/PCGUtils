#pragma once

#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGSettings.h"

#include "PCGGetPathData.generated.h"

/** Collects point paths from actor components implementing PCGPathProvider. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGGetPathDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetPathDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetPathData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetPathElementSettingsConfiguration PathSettings;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetPathElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
