#pragma once
#include "CoreMinimal.h"
#include "PCGGraph.h"
#include "Elements/PCGMeshSampler.h"
#include "PCGDataAsset.h"
#include "GeometryScript/MeshSamplingFunctions.h"
#include "PCGHelperTypes.generated.h"







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
struct PCGUTILS_API FPCGUtilsMeshSamplerSettings
{
	GENERATED_BODY();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	EPCGMeshSamplingMethod SamplingMethod = EPCGMeshSamplingMethod::OnePointPerTriangle;

	/** Will extract the color channel into the density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Color & Density", meta = (PCG_Overridable, InlineEditConditionToggle, PCG_OverrideAliases="bUseRedAsDensity"))
	bool bUseColorChannelAsDensity = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Color & Density", meta = (PCG_Overridable, EditCondition="bUseColorChannelAsDensity"))
	EPCGColorChannel ColorChannelAsDensity = EPCGColorChannel::Red;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Color & Density", meta = (InlineEditConditionToggle, PCG_Overridable))
	bool bHasDefaultColorOverride = true;

	/** When the mesh doesn't have any vertex color, will use this color instead. Otherwise, it will use (0,0,0,0) for "per point" and white for the rest. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Color & Density", meta = (PCG_Overridable, EditCondition=bHasDefaultColorOverride))
	FLinearColor DefaultColorOverride = FLinearColor::White;

	/** Enable voxelisation as a preparation pass. Can be more expensive given the VoxelSize. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Voxelize Options", meta = (PCG_Overridable))
	bool bVoxelize = false;

	/** Size of a voxel for the voxelization. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Voxelize Options", meta = (PCG_Overridable, EditCondition = "bVoxelize"))
	float VoxelSize = 100.0f;

	/** Post-processing pass after voxelization to remove hidden triangles. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Voxelize Options", meta = (PCG_Overridable, EditCondition = "bVoxelize"))
	bool bRemoveHiddenTriangles = true;

	/** LOD type to use when creating DynamicMesh from specified StaticMesh/SkeletalMesh. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|LODSettings", meta = (PCG_Overridable))
	EGeometryScriptLODType RequestedLODType = EGeometryScriptLODType::RenderData;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|LODSettings", meta = (PCG_Overridable))
	int32 RequestedLODIndex = 0;

	// Poisson Sampling parameters
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Poisson sampling", meta = (PCG_Overridable, EditCondition = "SamplingMethod == EPCGMeshSamplingMethod::PoissonSampling"))
	FGeometryScriptMeshPointSamplingOptions SamplingOptions;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Poisson sampling", meta = (PCG_Overridable, EditCondition = "SamplingMethod == EPCGMeshSamplingMethod::PoissonSampling"))
	FGeometryScriptNonUniformPointSamplingOptions NonUniformSamplingOptions;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex"))
	bool bExtractUVAsAttribute = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bExtractUVAsAttribute", EditConditionHides))
	FName UVAttributeName = TEXT("UV");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bExtractUVAsAttribute", EditConditionHides))
	int32 UVChannel = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex"))
	bool bOutputTriangleIds = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputTriangleIds", EditConditionHides))
	FName TriangleIdAttributeName = TEXT("TriangleId");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex"))
	bool bOutputMaterialInfo = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputMaterialInfo", EditConditionHides))
	FName MaterialIdAttributeName = TEXT("MaterialId");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extra", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputMaterialInfo", EditConditionHides))
	FName MaterialAttributeName = TEXT("Material");
};



