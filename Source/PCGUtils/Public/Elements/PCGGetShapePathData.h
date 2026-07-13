#pragma once

#include "PCGSettings.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetShapePathData.generated.h"

/**
 * Collects UShapePathComponent path points from actors and outputs them as PCG PointData.
 * One UPCGPointData per component — each path point becomes one PCG point in world space.
 * If the component's PreProcessShapePath override graph is active, its path is stamped into
 * the @Data metadata root so downstream PCG graphs can resolve it.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGGetShapePathSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()
public:
	UPCGGetShapePathSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetShapePathData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Actor Reference"))
	bool bOutputActorReference = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Component Reference"))
	bool bOutputComponentReference = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractPreProcessPathGraph = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractPreProcessPathGraph"))
	FString PreProcessPathGraphAttributeName = TEXT("ProcessPathGraph");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractPathHeight = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractPathHeight"))
	FString PathHeightAttributeName = TEXT("PathHeight");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractPathGroup = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractPathGroup"))
	FString PathGroupAttributeName = TEXT("PathGroup");
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractPathColorAttribute = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractPathColorAttribute"))
	FString PathColorAttribute = TEXT("PathColor");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractPathDensityAttribute = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractPathDensityAttribute"))
	FString PathDensityAttribute = TEXT("PathDensity");

protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetShapePathElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
