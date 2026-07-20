#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGSettings.h"

#include "PCGGetOverrideGraphSets.generated.h"

UENUM(BlueprintType)
enum class EPCGMetadataDomainTarget : uint8
{
	Data,
	Elements,
	Any
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|Graph Utilities")
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
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	EPCGMetadataDomainTarget TargetDomain = EPCGMetadataDomainTarget::Data;
};

class PCGUTILS_API FPCGGetOverrideGraphSetsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
