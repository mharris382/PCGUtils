#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Elements/PCGMeshSampler.h"
#include "GeometryScript/MeshSamplingFunctions.h"

#include "PCGDynMeshSample.generated.h"

/**
 * Samples PCG Points from the surface of a PCGUtilsDynMesh DynMesh, or from only the geometry referenced by a
 * DynMesh Selection (see FPCGUtilsMeshTargetFunctions for the shared Mesh/Selection input pin).
 *
 * Mirrors the sampling-stage behavior of Unreal's vanilla PCGGeometryScriptInterop Mesh Sampler node (sampling
 * method, color/density, UV, triangle id, material info, voxelization) so graphs can swap "Mesh Sampler" for
 * "Sample DynMesh" without learning a new metadata convention. It intentionally does not expose the vanilla
 * node's mesh-acquisition settings (Mesh/InputSource/RequestedLODType/RequestedLODIndex/SynchronousLoad) since
 * the source geometry is always resolved directly from PCGUtilsDynMesh data, never loaded/converted from a
 * Static/Skeletal Mesh.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynMeshSampleSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMesh|Sample"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }

	// Native "Add Node" categorization only offers the fixed EPCGSettingsType buckets (no free-form category
	// string is available to a native C++ element) - DynamicMesh is the closest match to "Utils DynMesh|Sampling".
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
#endif

	virtual bool UseSeed() const override { return true; }

	/** Matches the vanilla Mesh Sampler's One Point Per Triangle / One Point Per Vertex / Poisson Sampling modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sampling", meta = (PCG_Overridable))
	EPCGMeshSamplingMethod SamplingMethod = EPCGMeshSamplingMethod::OnePointPerTriangle;

	/** Will extract the color channel into the density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color & Density", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bUseColorChannelAsDensity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color & Density", meta = (PCG_Overridable, EditCondition = "bUseColorChannelAsDensity"))
	EPCGColorChannel ColorChannelAsDensity = EPCGColorChannel::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color & Density", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bHasDefaultColorOverride = true;

	/** When the sampled geometry has no vertex color, use this color instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color & Density", meta = (PCG_Overridable, EditCondition = "bHasDefaultColorOverride"))
	FLinearColor DefaultColorOverride = FLinearColor::White;

	/** Enable voxelization as a preparation pass before sampling. Can be more expensive given the VoxelSize.
	 *  Note: voxelization regenerates the mesh, so bOutputTriangleIds cannot refer back to the source DynMesh
	 *  when this is enabled (same limitation as the vanilla Mesh Sampler). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxelize Options", meta = (PCG_Overridable))
	bool bVoxelize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxelize Options", meta = (PCG_Overridable, EditCondition = "bVoxelize"))
	float VoxelSize = 100.0f;

	/** Post-processing pass after voxelization to remove hidden triangles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxelize Options", meta = (PCG_Overridable, EditCondition = "bVoxelize"))
	bool bRemoveHiddenTriangles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poisson Sampling", meta = (PCG_Overridable, EditCondition = "SamplingMethod == EPCGMeshSamplingMethod::PoissonSampling", EditConditionHides))
	FGeometryScriptMeshPointSamplingOptions SamplingOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poisson Sampling", meta = (PCG_Overridable, EditCondition = "SamplingMethod == EPCGMeshSamplingMethod::PoissonSampling", EditConditionHides))
	FGeometryScriptNonUniformPointSamplingOptions NonUniformSamplingOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex", EditConditionHides))
	bool bExtractUVAsAttribute = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bExtractUVAsAttribute", EditConditionHides))
	FName UVAttributeName = TEXT("UV");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UVs", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bExtractUVAsAttribute", EditConditionHides))
	int32 UVChannel = 0;

	/** Not available for One Point Per Vertex: a mesh vertex may belong to several triangles, so there is no
	 *  single triangle id to report (matches vanilla Mesh Sampler behavior). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangle Info", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex", EditConditionHides))
	bool bOutputTriangleIds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triangle Info", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputTriangleIds", EditConditionHides))
	FName TriangleIdAttributeName = TEXT("TriangleId");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex", EditConditionHides))
	bool bOutputMaterialInfo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputMaterialInfo", EditConditionHides))
	FName MaterialIdAttributeName = TEXT("MaterialId");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info", meta = (PCG_Overridable, EditCondition = "SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex && bOutputMaterialInfo", EditConditionHides))
	FName MaterialAttributeName = TEXT("Material");

	/** Each PCG point represents a discretized, volumetric region of world space. The points' Steepness value
	 *  [0.0 to 1.0] establishes how "hard" or "soft" that volume will be represented. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Points", meta = (ClampMin = "0", ClampMax = "1", PCG_Overridable))
	float PointSteepness = 1.0f;

	/** Transforms sampled point positions/orientations/bounds from the DynMesh's local/reference space into
	 *  world space, using the target actor transform resolved for the source DynMesh data. When disabled,
	 *  points remain in the DynMesh's local coordinate space. Applied as a final pass after sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Space", meta = (PCG_Overridable))
	bool bTransformToWorldSpace = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshSampleElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	// Loading target-actor transforms and reading from the source DynMesh should stay on the main thread,
	// matching the other PCGUtilsDynMesh Dynamic Mesh elements.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
