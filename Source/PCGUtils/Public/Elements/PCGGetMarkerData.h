// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGGetMarkerData.generated.h"

class UPCGMarkerComponent;

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
	
	//virtual bool DisplayModeSettings() const override { return false; }
#endif
	
protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetPointElementSettingsConfiguration PointSettings;
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
	
	virtual void AddMetadataFromMarker(FPCGContext* context, 
		const UPCGGetMarkerDataSettings* settings,
		const  AActor* actor,
		const UPCGMarkerComponent* marker, 
		UPCGMetadata* mutableMetadata) const;
	
	virtual bool ShouldFilterMarker(const UPCGGetMarkerDataSettings* settings, const UPCGMarkerComponent* marker ) const { return true; }
};
