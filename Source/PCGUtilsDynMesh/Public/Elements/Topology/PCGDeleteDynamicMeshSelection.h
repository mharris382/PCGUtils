#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDeleteDynamicMeshSelection.generated.h"

/** Which GeometryScript mesh-edit operation to delegate the deletion to. */
UENUM(BlueprintType)
enum class EPCGDeleteDynamicMeshSelectionMode : uint8
{
	/** Deletes exactly the selected triangles (UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteSelectedTrianglesFromMesh). Vertices still used by other, unselected triangles are left intact. */
	Triangles,

	/** Deletes every vertex touched by the selection (UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteVerticesFromMesh), which also removes all triangles incident to those vertices - a broader deletion than Triangles mode when the selection's triangles share vertices with unselected triangles. */
	Vertices
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDeleteDynamicMeshSelectionSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	virtual bool RequiresSelection() const override { return true; }
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = DeleteMode == EPCGDeleteDynamicMeshSelectionMode::Triangles
			? UE::Geometry::EGeometryElementType::Face : UE::Geometry::EGeometryElementType::Vertex;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DeleteDynMeshSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Whether to delete the selected triangles only, or every vertex the selection touches (and, transitively, every triangle incident to those vertices). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EPCGDeleteDynamicMeshSelectionMode DeleteMode = EPCGDeleteDynamicMeshSelectionMode::Triangles;
};

class PCGUTILSDYNMESH_API FPCGDeleteDynamicMeshSelectionElement : public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
