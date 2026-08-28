#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/MeshRemeshFunctions.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGRemesh.generated.h"

UENUM(BlueprintType)
enum class EPCGRemeshMode : uint8
{
	/** Wraps UGeometryScriptLibrary_RemeshingFunctions::ApplyUniformRemesh. */
	Uniform,
	/** Wraps UGeometryScriptLibrary_RemeshingFunctions::ApplyAdaptiveRemesh. */
	Adaptive
};

/**
 * Applies Geometry Script Uniform or Adaptive remeshing to Dynamic Mesh data. The Mesh input pin accepts either a
 * whole Dynamic Mesh or a PCGUtilsDynMesh Mesh Selection - for a Selection, only the selected region is remeshed
 * (with its outer boundary held fixed) and welded back into the untouched remainder of the source mesh, via the
 * shared PCGUtilsDynMesh Mesh Target Handle infrastructure (see MeshTarget/PCGUtilsMeshTargetFunctions.h).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGRemeshSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("Remesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Remesh", meta = (PCG_Overridable))
	EPCGRemeshMode Mode = EPCGRemeshMode::Uniform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Remesh Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptRemeshOptions RemeshOptions;

	/** Purely informational: when the Mesh input is a Mesh Selection, the extracted region's outer boundary is always held Fixed (regardless of Mesh Boundary Constraint above) so it can be welded back exactly. Group/Material boundary constraints above still apply normally. This has no effect on a whole Dynamic Mesh input. */
	UPROPERTY(VisibleAnywhere, Category = "Remesh Options")
	FText SelectionModeBoundaryNotice = NSLOCTEXT("PCGRemesh", "SelectionBoundaryNotice",
		"When the Mesh input is a Mesh Selection, the extracted region's outer boundary always uses a Fixed constraint so it welds back exactly, regardless of Mesh Boundary Constraint above.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Uniform Options",
		meta = (PCG_Overridable, EditCondition = "Mode==EPCGRemeshMode::Uniform", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptUniformRemeshOptions UniformOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Options",
		meta = (PCG_Overridable, EditCondition = "Mode==EPCGRemeshMode::Adaptive", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptAdaptiveRemeshOptions AdaptiveOptions;

	/** Adaptive Remesh always requires a weight map internally. If disabled (or the named map below is missing), a neutral map is used automatically, so Relative Density above has no directional effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weight Map",
		meta = (PCG_Overridable, EditCondition = "Mode==EPCGRemeshMode::Adaptive", EditConditionHides))
	bool bUseAdaptiveWeightMap = false;

	/** Name of the Dynamic Mesh vertex weight-map layer to read (see PCGUtilsDynMesh weight-map conventions). Values are clamped [0,1] by Geometry Script; higher values increase density when Relative Density is positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weight Map",
		meta = (PCG_Overridable, EditCondition = "Mode==EPCGRemeshMode::Adaptive && bUseAdaptiveWeightMap", EditConditionHides))
	FName AdaptiveWeightMapAttributeName = "RemeshDensityWeight";

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FName GetMainInputPinLabel() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/**
 * Uniform / Adaptive remeshing. A selection is remeshed as an extracted region with a fixed outer boundary and
 * welded back, which is why this one still goes through the Mesh Target layer.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshRemeshOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EPCGRemeshMode Mode = EPCGRemeshMode::Uniform;
	FGeometryScriptRemeshOptions RemeshOptions;
	FGeometryScriptUniformRemeshOptions UniformOptions;
	FGeometryScriptAdaptiveRemeshOptions AdaptiveOptions;
	bool bUseAdaptiveWeightMap = false;
	FName AdaptiveWeightMapAttributeName;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGRemeshElement : public FPCGUtilsDynMeshProcessBaseElement
{
};
