#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGSeparateSelection.generated.h"

/** Which GeometryScript mesh-edit operation to delegate the split to, mirroring Delete Mesh Selection's Delete Mode. */
UENUM(BlueprintType)
enum class EPCGDynMeshSeparationMode : uint8
{
	/** Splits along exactly the selected/unselected triangles. Vertices shared across the split are duplicated onto both outputs. */
	Triangles,

	/** Splits along every vertex touched by the selection, which also removes all triangles incident to those vertices from the opposite output - a cleaner cut than Triangles mode when the selection's triangles share vertices with the rest of the mesh. */
	Vertices
};

/**
 * Splits a Dynamic Mesh Selection's source mesh into two independent Dynamic Mesh outputs: Selected (only the
 * selected elements) and Unselected (everything else). Each output is a full deep copy of the source mesh with the
 * opposite side deleted via GeometryScript - no region extraction/weld is involved, since the two halves are meant
 * to end up as separate meshes rather than being recombined.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGSeparateSelectionSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SeparateSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Whether to split along the exact selected elements, or every vertex the selection touches (and, transitively, every triangle incident to those vertices). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGDynMeshSeparationMode SeparationMode = EPCGDynMeshSeparationMode::Triangles;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSeparateSelectionElement : public IPCGDynamicMeshBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
