#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshSelectionProcessBase.h"
#include "Materials/MaterialInterface.h"

#include "PCGMaterial.generated.h"

/** Assigns a material to an entire Dynamic Mesh or appends it for a mesh selection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGMaterialSettings : public UPCGDynamicMeshSelectionProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("Material"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Material assigned to the whole mesh or to the supplied selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Slot-zero material used when a selection is applied to a mesh with no established material assignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialInterface> DefaultMaterial;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGMaterialElement : public FPCGDynamicMeshSelectionProcessBaseElement
{
protected:
	virtual bool ProcessMesh(UPCGDynamicMeshData* MeshData,
		const UPCGDynamicMeshSelectionData* SelectionData, FPCGContext* Context) const override;
};
