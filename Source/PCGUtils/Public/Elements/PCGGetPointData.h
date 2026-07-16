#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetPointData.generated.h"

/** Collects point data from components implementing PCGPointProvider. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGGetPointDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetPointDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetPointData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ShowOnlyInnerProperties))
	FGetPointElementSettingsConfiguration PointSettings;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetPointDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
