#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

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

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDeleteDynamicMeshSelectionSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DeleteDynamicMeshSelection"); }
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

class PCGUTILSDYNMESH_API FPCGDeleteDynamicMeshSelectionElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
