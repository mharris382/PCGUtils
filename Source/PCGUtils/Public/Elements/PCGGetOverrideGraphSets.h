#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGSettings.h"

#include "PCGGetOverrideGraphSets.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGGetOverrideGraphSetsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetOverrideGraphSets")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	FName OverrideGraphAttributeName = FName(TEXT("PointOverride"));
};

class PCGUTILS_API FPCGGetOverrideGraphSetsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
