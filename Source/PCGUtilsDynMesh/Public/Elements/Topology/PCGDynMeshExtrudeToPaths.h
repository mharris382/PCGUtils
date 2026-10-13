// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"

#include "PCGDynMeshExtrudeToPaths.generated.h"

namespace PCGDynMeshExtrudeToPathsConstants
{
	inline const FName PathsPin = TEXT("Paths");
}

/** What the vertices inside the selected faces do when their rim is moved to the paths. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshExtrudeToPathsInterior : uint8
{
	/** They stay where they are. Right for an inset, where the rim moves within the surface. */
	Keep UMETA(DisplayName="Keep In Place"),
	/** They move by the average of how far the rim of their area moved. Right for an extrude, where the faces are carried along. */
	FollowRim UMETA(DisplayName="Follow The Rim")
};

/**
 * Extrudes or insets the selected faces to a rim you supply as paths.
 *
 * The selected faces are separated from the faces around them along their boundary and joined back with a strip of
 * new faces, as Extrude Faces and Inset Faces do. The difference is where the separated rim goes: each of its
 * vertices is moved to a path point, instead of along a direction or by a distance. Whether the result is an
 * inset, an extrude or something in between is decided only by where the points are.
 *
 * The round trip: DynMesh | Selection To Paths with Output Vertex Index on, any path nodes that move the points
 * (offset, smooth, relax, project), then this node with the same mesh and selection. A point is matched to its
 * rim vertex by the vertex ID it carries, not by order or count, so the path nodes may reorder points. A rim
 * vertex no point names stays where it is, which leaves a strip of no width there. A point naming a vertex that
 * is not on the rim of the selection is ignored. Both are reported in the log with counts. One point per rim
 * vertex is the intended use; adding or removing rim vertices is not supported.
 *
 * Requires a selection: with the whole mesh selected there is no rim.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh selection selector extrude inset offset faces path paths rim boundary vertex id"))
class PCGUTILSDYNMESH_API UPCGDynMeshExtrudeToPathsSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExtrudeToPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Point attribute (int32, required on every point of Paths) naming the mesh vertex the point belongs to. It is
	 * what DynMesh | Selection To Paths writes when Output Vertex Index is on, under the same default name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/**
	 * The path points are in world space and are brought into the mesh's own space (the PCG target actor's) before
	 * they are used. Leave on when the paths came from Selection To Paths with Output To World Space on, which is
	 * its default. Turn off when they were output mesh-local.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	bool bPathsInWorldSpace = true;

	/** What the vertices inside the selected faces do. Faces with no vertex of their own inside are not affected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	EPCGUtilsDynMeshExtrudeToPathsInterior InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;

	/** UVs of the new strip faces are planar-projected and scaled by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	float UVScale = 1.0f;

	/** Which faces come out as the result selection: the faces that were selected, the new strip, or both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;

	virtual bool RequiresSelection() const override { return true; }
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* Context) const override;
};
