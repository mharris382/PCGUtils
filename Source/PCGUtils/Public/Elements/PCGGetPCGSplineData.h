#pragma once

#include "PCGSettings.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGTypedGetter.h"

#include "PCGGetPCGSplineData.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGGetPCGSplineDataSettings : public UPCGGetSplineSettings
{
	GENERATED_BODY()

public:
	UPCGGetPCGSplineDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetPCGSplineData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetPathElementSettingsConfiguration PathSettings;
};

class PCGUTILS_API FPCGGetPCGSplineDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
