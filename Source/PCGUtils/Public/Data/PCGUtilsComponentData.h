// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OverrideGraphs.h"
#include "UObject/ObjectMacros.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/PCGMetadataCommon.h"
#include "PCGUtilsComponentData.generated.h"

// USTRUCT(BlueprintType)
// struct PCGUTILS_API FPCGEffectorData
// {
// 	GENERATED_BODY()
// 	
// 	float EffectorFalloff = 1.0f;
// 	float EffectorDistance = 1000.0f;
// 	
// };

/**
 * 
 */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPathComponentData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	FPCGOverrideGraph ProcessPathGraph;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (DisplayName = "PathGroup", ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
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
	
	
	float GetPathDensity() const { return bSetPathDensity ? PathDensity : 1.0f; }
	FLinearColor GetPathColor() const { return bSetPathColor ? PathColor : FLinearColor::White; }
	
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


USTRUCT(BlueprintType)
struct PCGUTILS_API FPointComponentData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	FPCGOverrideGraph ProcessPointGraph;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (DisplayName = "GroupID", ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;
	


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPointDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPointDensity", UIMin=0, UIMax=1))
	float PointDensity = 1.0f;

	/** Controls the size of the full-density core inside the point bounds. Lower values produce softer edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float Steepness = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPointColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPointColor"))
	FLinearColor PointColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
	
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (DisplayName = "Point Falloff", ToolTip = "General Purpose falloff value. intended for cases where the marker has an influence radius"))
	//float PointFalloff = 0.0f;
	
	float GetPointDensity() const { return bSetPointDensity ? PointDensity : 1.0f; }
	FLinearColor GetPointColor() const { return bSetPointColor ? PointColor : FLinearColor::White; }
};

UENUM(BlueprintType)
enum class EPCGUtilsPointMetadataDomain : uint8
{
	Data,
	Elements
};

USTRUCT(BlueprintType)
struct PCGUTILS_API FGetPointElementSettingsConfiguration
{
	GENERATED_BODY()

	/** Domain used for per-point attributes. Process Point Graph is always written to @Data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EPCGUtilsPointMetadataDomain AttributeDomain = EPCGUtilsPointMetadataDomain::Elements;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bExtractProcessPointGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bExtractProcessPointGraph"))
	FString ProcessPointGraphAttributeName = TEXT("PointOverride");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractGroup = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractGroup"))
	FString GroupAttributeName = TEXT("GroupID");
	
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	//bool bExtractFalloff = false;
	//
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition = "bExtractFalloff"))
	//FString FalloffAttributeName = TEXT("PointFalloff");
};

USTRUCT(BlueprintType)
struct PCGUTILS_API FGetComponentDataSettings
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOutputActorReference = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bOutputActorReference", EditConditionHides))
	FString ActorReferenceAttributeName = TEXT("ActorReference");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOutputComponentReference = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bOutputComponentReference", EditConditionHides))
	FString ComponentReferenceAttributeName = TEXT("ComponentReference");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (DisplayName = "Output Component Relative Transform"))
	bool bOutputComponentTransform = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "bOutputComponentTransform", EditConditionHides))
	FString RelativeTransformAttributeName = TEXT("RelativeTransform");
};



UCLASS()
class PCGUTILS_API UPCGUtilPathDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	static void GetPathDataFromSettings(UPCGMetadata* Meta, const FGetPathElementSettingsConfiguration* Settings, const FPathComponentData* Data);
	static void GetComponentDataFromSettings(UPCGMetadata* Meta, const FGetComponentDataSettings* Settings, USceneComponent* Data);
	
	static void GetPointDataFromSettings(UPCGMetadata* Meta, const FGetPointElementSettingsConfiguration* Settings, const FPointComponentData* Data, const FPCGMetadataDomainID Domain = PCGMetadataDomainID::Data, const int64 Key = -1);
//	static bool AssignGroupIDAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignPathHeightAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignProcessPathGraphAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data);
//	static bool AssignPathColorAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data, FLinearColor DefaultColor = FLinearColor::White, bool bAlwaysCreateAttribute = false);
//	static bool AssignPathDensityAttribute(UPCGMetadata* Meta, FName AttributeName, const FPathComponentData* Data, float DefaultDensity = 1.0f, bool bAlwaysCreateAttribute = false);
};
