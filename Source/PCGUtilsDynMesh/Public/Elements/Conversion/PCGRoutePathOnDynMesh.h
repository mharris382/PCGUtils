// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGRoutePathOnDynMesh.generated.h"

namespace PCGRoutePathOnDynMeshConstants
{
	inline const FName MeshInputPin = TEXT("Mesh");
	inline const FName PathInputPin = TEXT("Path");
	inline const FName PathsOutputPin = TEXT("Paths");
}

/**
 * Turns a sparse guide path into a dense path that follows a DynMesh surface.
 *
 * Every guide point is projected onto the mesh once, then each consecutive pair of projected anchors is joined
 * by the shortest path across the actual triangle surface (a geodesic). The segments are concatenated into one
 * ordered path, so an authored A - B - C guide becomes a continuous mesh-constrained route.
 *
 * Intended for climbing vines, cables and ropes routed over generated architecture, cracks, erosion streaks and
 * other surface detailing where the path must hug the geometry rather than cut through space. This is a direct
 * bridge from PCG paths to DynMesh topology, not a general graph-pathfinding system: there are no heuristics,
 * costs, obstacles or flood fills here.
 *
 * A DynMesh Selection (or a connected Selector) restricts routing to the selected triangle region, which is how
 * a vine is kept to one wall of a larger mesh.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="geodesic path route surface vine cable crack shortest"))
class PCGUTILSDYNMESH_API UPCGRoutePathOnDynMeshSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** Routing runs across triangles, so any incoming vertex/edge selection converts to its incident faces. */
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("RoutePathOnDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Treat the incoming guide points as world space and emit the routed path in world space, converting through
	 * the DynMesh's target actor transform. Disable when the guide path is already in the mesh's own coordinate
	 * space. Follows the PCGUtilsDynMesh world/local convention (see PCGUtilsDynMeshSpaceHelpers).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bWorldSpace = true;

	/**
	 * Farthest a guide point may sit from the mesh and still be accepted as an anchor, in mesh-local units.
	 * Zero means unlimited, matching the module's existing distance-limit convention, and lets a sparse authored
	 * guide path float well off the generated geometry. A guide point beyond this distance fails its adjoining
	 * segments with a graph warning instead of snapping to a far-away part of the mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routing", meta=(PCG_Overridable, ClampMin="0"))
	double MaxProjectionDistance = 0.0;

	/**
	 * Also route from the last guide point back to the first when the input path reports itself closed through
	 * Is Closed Attribute Name. The output then repeats no point at the join, matching the closed-path
	 * convention used by DynMesh Selection To Paths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routing", meta=(PCG_Overridable))
	bool bRouteClosedPaths = true;

	/**
	 * Name of the @Data Bool closed-loop attribute. This is a hard contract with the rest of the PCGUtils path
	 * toolset: it is read from the input path to decide whether to close the route, and always written to the
	 * output so downstream path consumers can tell open from closed. Optional on input (absent means open);
	 * nothing is written to the output when this is None.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routing", meta=(PCG_Overridable))
	FName IsClosedAttributeName = TEXT("IsClosed");

	/** Steepness assigned to every generated path point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Points", meta=(PCG_Overridable, ClampMin="0", ClampMax="1"))
	float PointSteepness = 1.0f;

protected:
	virtual FName GetMainInputPinLabel() const override { return PCGRoutePathOnDynMeshConstants::MeshInputPin; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGRoutePathOnDynMeshElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	/**
	 * The geodesic solver itself is worker-thread safe (it only touches an owned FDynamicMesh3), but resolving
	 * PCG data and the DynMesh target actor transform is not - matching every other PCGUtilsDynMesh element.
	 */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
