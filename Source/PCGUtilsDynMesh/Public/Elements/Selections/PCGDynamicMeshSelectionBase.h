#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGSettings.h"
#include "Selections/GeometrySelection.h"

#include "PCGDynamicMeshSelectionBase.generated.h"

class UPCGDynamicMeshData;
namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Whole-mesh or incoming-selection evaluation domain shared by all materialized selection nodes. Incoming
 * selections are converted lazily with GeometryScript's inclusive domain rules by the Process* method used by
 * the node, so expensive predicates only run on converted candidates.
 */
class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionCandidates
{
public:
	FPCGDynamicMeshSelectionCandidates(const UE::Geometry::FDynamicMesh3& InMesh,
		const UE::Geometry::FGeometrySelection* InSelection);

	bool IsRestricted() const { return bHasIncomingSelection; }

	void ProcessVertices(TFunctionRef<void(int32)> Function) const;
	void ProcessEdges(TFunctionRef<void(int32)> Function) const;
	void ProcessTriangles(TFunctionRef<void(int32)> Function) const;

	/** Materializes the candidate set in a Triangle-topology element domain for defensive result clamping. */
	bool BuildSelection(UE::Geometry::EGeometryElementType ElementType,
		UE::Geometry::FGeometrySelection& OutSelection) const;

private:
	const UE::Geometry::FDynamicMesh3& Mesh;
	FGeometryScriptMeshSelection ScriptSelection;
	bool bHasIncomingSelection = false;
};

namespace PCGDynamicMeshSelectionConstants
{
	inline const FName MeshInputPin = TEXT("Mesh");
	inline const FName SelectionOutputPin = TEXT("Selection");
}

/** Common pin contract for nodes that author a selection from a PCG Dynamic Mesh or narrow an existing selection. */
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
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const = 0;
};
