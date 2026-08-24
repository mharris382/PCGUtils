#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"
#include "Selections/GeometrySelection.h"

#include "PCGDynamicMeshSelectionFilterBase.generated.h"

class UPCGDynamicMeshData;
namespace UE::Geometry { class FDynamicMesh3; }

namespace PCGDynamicMeshSelectionFilterConstants
{
	inline const FName InputPin = TEXT("In");
}

/**
 * Common base for nodes that compute a Dynamic Mesh element selection and either output it directly, or - if fed
 * an existing Dynamic Mesh Selection instead of a bare Dynamic Mesh - evaluate only the incoming selection after
 * converting it to the node's output domain.
 *
 * This is the "filter an existing selection, or start fresh from the whole mesh" counterpart to
 * PCGDynamicMeshSelectionBase (which only ever starts fresh from a Mesh pin). It exists because chaining multiple
 * selection queries (eg Select in Point Bounds -> Sharp Edge Filter -> Edge Direction) requires each step to be
 * able to consume either kind of upstream data through a single pin, matching how PCG selection-consuming nodes
 * elsewhere in this module resolve their source mesh from the selection itself rather than a separate Mesh pin.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionFilterBaseSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};

/**
 * Executes the common mesh/selection resolution, candidate-domain match computation, and output creation path.
 * Resolution is read-only (never copies the mesh) - unlike PCGUtilsMeshTargetFunctions, which always produces a
 * mutable working copy for mutation-oriented elements, filter nodes only ever read the mesh to compute a Selection.
 */
class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionFilterBaseElement : public IPCGDynamicMeshBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	/**
	 * Computes the matching mesh elements inside Candidates. Implementations should call
	 * OutSelection.InitializeTypes(...) to declare the element/topology type they produce, then use the matching
	 * Candidates.Process* method rather than iterating the whole mesh directly.
	 * @return false to skip this input entirely (eg on a hard error already logged by the implementation).
	 */
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const = 0;
};

namespace PCGDynamicMeshSelectionFilterHelpers
{
	/**
	 * Adds EdgeID to a Triangle-topology Edge FGeometrySelection, encoding both half-edge representations
	 * (FMeshTriEdgeID) per the convention documented on FGeometryScriptMeshSelection - non-boundary edges are
	 * represented twice, once per adjacent triangle.
	 */
	PCGUTILSDYNMESH_API void AddEdgeToSelection(const UE::Geometry::FDynamicMesh3& Mesh, int32 EdgeID,
		UE::Geometry::FGeometrySelection& OutSelection);
}
