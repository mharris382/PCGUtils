#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/MeshRemeshFunctions.h"
#include "PCGSettings.h"

#include "PCGRemesh.generated.h"

UENUM(BlueprintType)
enum class EPCGRemeshMode : uint8
{
	/** Wraps UGeometryScriptLibrary_RemeshingFunctions::ApplyUniformRemesh. */
	Uniform,
	/** Wraps UGeometryScriptLibrary_RemeshingFunctions::ApplyAdaptiveRemesh. */
	Adaptive
};

UENUM(BlueprintType)
enum class EPCGRemeshInputMode : uint8
{
	/** Remeshes the entire input Dynamic Mesh. */
	DynamicMesh,
	/** Remeshes only the region described by an input Mesh Selection; the rest of the source mesh is preserved exactly and the region is welded back in. */
	MeshSelection
};

/**
 * Applies Geometry Script Uniform or Adaptive remeshing, either to an entire Dynamic Mesh or to only the
 * triangle region described by a PCGUtilsDynMesh Mesh Selection. In Mesh Selection mode, the selected region is
 * extracted, remeshed with its outer boundary held fixed, and welded back into the untouched remainder of the
 * source mesh.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGRemeshSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("Remesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	/** Whole mesh: the normal Dynamic Mesh pin is remeshed entirely. Mesh Selection: only the selected region is remeshed and welded back into the untouched remainder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Remesh", meta = (PCG_Overridable))
	EPCGRemeshInputMode InputMode = EPCGRemeshInputMode::DynamicMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Remesh", meta = (PCG_Overridable))
	EPCGRemeshMode Mode = EPCGRemeshMode::Uniform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Remesh Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptRemeshOptions RemeshOptions;

	/** Purely informational: in Mesh Selection mode, the extracted region's outer boundary is always held Fixed (regardless of Mesh Boundary Constraint above) so it can be welded back exactly. Group/Material boundary constraints above still apply normally. */
	UPROPERTY(VisibleAnywhere, Category = "Remesh Options",
		meta = (PCG_Overridable, EditCondition = "InputMode==EPCGRemeshInputMode::MeshSelection", EditConditionHides))
	FText SelectionModeBoundaryNotice = NSLOCTEXT("PCGRemesh", "SelectionBoundaryNotice",
		"Mesh Selection mode always uses a Fixed outer boundary on the extracted region so it welds back exactly. Mesh Boundary Constraint above only applies in Dynamic Mesh (whole-mesh) mode.");

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

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGRemeshElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
