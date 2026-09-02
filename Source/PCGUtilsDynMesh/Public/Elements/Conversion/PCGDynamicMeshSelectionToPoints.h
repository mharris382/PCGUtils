#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGDynamicMeshSelectionToPoints.generated.h"

/**
 * Converts an existing Dynamic Mesh Selection's unique mesh vertices to PCG points.
 *
 * Documented exception to the module's standard unified DynMesh/Selection + Selector input contract
 * (see PCGUtilsDynMesh/AGENTS.md, "Every operation should support DynMesh Selection data and the optional
 * Selector input"): this node's whole purpose is converting an *already materialized* selection to points
 * without paying to copy or resolve the rest of the mesh, so it intentionally accepts only DynMesh Selection
 * data - no bare DynMesh input, no Selector pin. A bare mesh's vertices are already served by DynMesh To Points.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionToPointsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionToPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
		return true;
	}
#endif

	/** Transform mesh-local vertex positions and normals into the PCG target actor's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/** Add the source dynamic-mesh vertex ID to every generated point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bOutputVertexIndex = false;

	/** Attribute that receives the source dynamic-mesh vertex ID. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable, EditCondition = "bOutputVertexIndex"))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/** Include a vertex when any incident source element is selected. Disable to require full inclusion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bAllowPartialInclusion = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionToPointsElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
