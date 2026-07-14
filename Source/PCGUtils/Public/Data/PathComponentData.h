// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OverrideGraphs.h"
#include "UObject/ObjectMacros.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/PCGMetadataCommon.h"

#include "PathComponentData.generated.h"


/**
 * 
 */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPathComponentData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	FPCGOverrideGraph ProcessPathGraph;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (DisplayName = "PathWidth", ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;
	


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPathDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPathDensity", UIMin=0, UIMax=1))
	float PathDensity = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPathColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPathColor"))
	FLinearColor PathColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
	
	//TODO: use bool flag for gizmo drawing purposes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bHasPathHeight = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG" , meta = (DisplayName= "PathHeight",EditCondition="bHasPathHeight"))
	float Height = 0.0f;
	
	//TODO: use bool flag for gizmo drawing purposes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bHasPathWidth = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG" , meta = (DisplayName= "PathWidth",EditCondition="bHasPathWidth"))
	float PathWidth = 0.0f;
	
	
	float GetPathDensity(const float DefaultDensity) const { return bSetPathDensity ? PathDensity : DefaultDensity; }
	FLinearColor GetPathColor(const FLinearColor DefaultColor) const { return bSetPathColor ? PathColor : DefaultColor; }
	
};


USTRUCT(BlueprintType)
struct PCGUTILS_API FGetPathElementSettingsConfiguration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bExtractProcessPathGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bExtractProcessPathGraph"))
	FString ProcessPathGraphAttributeName = TEXT("ProcessPathGraph");


	
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractGroup = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractGroup"))
	FString GroupAttributeName = TEXT("PathGroup");
	
	
	
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractDensity"))
	FString PathDensityAttributeName = TEXT("PathDensity");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractColor"))
	FString ColorAttributeName = TEXT("PathColor");

	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bExtractHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bExtractHeight"))
	FString HeightAttributeName = TEXT("PathHeight");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bExtractWidth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bExtractWidth"))
	FString WidthAttributeName = TEXT("PathWidth");
};

UCLASS()
class PCGUTILS_API UPCGUtilPathDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	static void GetPathDataFromSettings(UPCGMetadata* Meta, const FGetPathElementSettingsConfiguration* Settings, const FPathComponentData* Data);
//	static bool AssignGroupIDAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignPathHeightAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignProcessPathGraphAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignPathColorAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data, FLinearColor DefaultColor = FLinearColor::White, bool bAlwaysCreateAttribute = false);
//	static bool AssignPathDensityAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data, float DefaultDensity = 1.0f, bool bAlwaysCreateAttribute = false);
};
