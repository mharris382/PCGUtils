// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGGetMarkerData.generated.h"

/**
 * 
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGGetMarkerDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()
	
public:
	UPCGGetMarkerDataSettings();
	
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetMarkerData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	
protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Marker Priority"))
	bool bOutputMarkerPriority = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Actor Reference"))
	bool bOutputActorReference = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Component Reference"))
	bool bOutputComponentReference = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	float DefaultPointDensity = 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	FLinearColor DefaultPointColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Output Override Graph"))
	bool bOutputOverrideGraph = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(DisplayName="Override Graph Attribute Name", EditCondition = "bOutputOverrideGraph"))
	FName OverrideOutputGraphName = FName(TEXT("OverrideGraph"));
	
protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};
	

class PCGUTILS_API FPCGGetMarkerDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
