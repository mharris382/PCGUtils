#pragma once
#include "CoreMinimal.h"
#include "PCGGraph.h"
#include "PCGDataAsset.h"
#include "PCGHelperTypes.generated.h"





USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGSurfaceSampleSettings
{
	GENERATED_BODY();

	FPCGSurfaceSampleSettings() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SurfaceSampleSettings")
	float PointsPerSquaredMeter = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceSampleSettings")
	FVector PointExtents = FVector(20, 20, 100);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SurfaceSampleSettings")
	float Looseness = 1.0;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Clutter", meta = (Tooltip = "If enabled a curved surface will be created at each shelf point, instead of a square one. This behavior can be overriden using special identifiers on the shelf points (tags or meshes)"))
	//bool UseRoundedSurfaceByDefault = false;
};



USTRUCT(BlueprintType)
struct PCGUTILS_API FTransformRandomSettings
{
	GENERATED_BODY();

	FTransformRandomSettings() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FVector OffsetMin = FVector(0.0, 0.0, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FVector OffsetMax = FVector(0.0, 0.0, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool AbsoluteOffset = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FRotator RotationMin = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings")
	FRotator RotationMax = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool AbsoluteRotation = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings", meta=(AllowPreserveRatio))
	FVector ScaleMin = FVector(1.0, 1.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Transform Settings", meta=(AllowPreserveRatio))
	FVector ScaleMax = FVector(1.0, 1.0, 1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transform Settings")
	bool bUniformScale = false;
	
};


USTRUCT(BlueprintType)
struct PCGUTILS_API FSimpleGrammarModule
{
	GENERATED_BODY();

	FSimpleGrammarModule() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module")
	FString Symbol;


    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module", meta=(InlineEditConditionToggle))
    bool bOverrideAssetSize = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module", meta=(AllowPreserveRatio, EditCondition="!bOverrideAssetSize"))
    FVector AssetSize3D = FVector(100, 100, 100);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module", meta=(AllowPreserveRatio))
	FVector Scale = FVector(1.0, 1.0, 1.0);

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module")
	FVector ModulePadding = FVector(0.0, 0.0, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module")
	bool Scalable = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module")
    FLinearColor DebugColor = FLinearColor(1.0,1.0,1.0,1.0);


    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module|Asset")
	bool SpawnDataAsset = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module|Asset", meta = (EditCondition = "!SpawnDataAsset", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Module|Asset", meta = (EditCondition = "SpawnDataAsset", EditConditionHides))
	TSoftObjectPtr<UPCGDataAsset> DataAsset;


};


UENUM(BlueprintType)
enum class EStaticMeshResourceType : uint8
{
	SingleMesh	   ,
	MeshArray	   ,
	MeshCollection 
};

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGStaticMeshResource
{
	GENERATED_BODY();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "StaticMeshResource")
	EStaticMeshResourceType Type;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "StaticMeshResource", meta = (EditCondition = "Type==EStaticMeshResourceType::SingleMesh", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> Mesh;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "StaticMeshResource", meta = (EditCondition = "Type==EStaticMeshResourceType::MeshArray", EditConditionHides))
	TArray<TSoftObjectPtr<UStaticMesh>> MeshArray;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "StaticMeshResource", meta = (EditCondition = "Type==EStaticMeshResourceType::MeshCollection", EditConditionHides))
	TSoftObjectPtr<UDataAsset> AssetCollection;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "StaticMeshResource")
	TSoftObjectPtr<UMaterialInterface> OverrideMaterial;
};