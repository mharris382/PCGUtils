#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Selections/GeometrySelection.h"

#include "PCGDynamicMeshSelectionBase.generated.h"

class UPCGDynamicMeshData;
namespace UE::Geometry { class FDynamicMesh3; }

namespace PCGDynamicMeshSelectionConstants
{
	inline const FName MeshInputPin = TEXT("Mesh");
	inline const FName SelectionOutputPin = TEXT("Selection");
}

/** Common pin contract for nodes that author a selection on a PCG Dynamic Mesh. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionBaseSettings : public UPCGSettings
{
	GENERATED_BODY()

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};

/** Executes the common mesh validation and selection-data creation path. */
class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionBaseElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool CreateSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const = 0;
};
