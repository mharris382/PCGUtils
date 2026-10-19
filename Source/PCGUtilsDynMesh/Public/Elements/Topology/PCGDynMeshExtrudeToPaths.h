// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"

#include "PCGDynMeshExtrudeToPaths.generated.h"

namespace PCGDynMeshExtrudeToPathsConstants
{
	inline const FName PathsPin = TEXT("Paths");
}

/** How the rim of the selected faces is matched to the paths. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshExtrudeToPathsMatching : uint8
{
	/**
	 * Each path is a closed outline, and the rim loop whose shape is closest to it is fitted to it. Point count, order,
	 * start and winding do not matter and no attribute is needed, so the paths can come from any path operation
	 * (offsets, booleans, smoothing, resampling).
	 */
	ByShape UMETA(DisplayName="By Shape"),
	/**
	 * Each path point names the rim vertex it moves by vertex ID. Needs paths from DynMesh | Selection To Paths with
	 * Output Vertex Index on, one point per rim vertex.
	 */
	ByVertexID UMETA(DisplayName="By Vertex ID")
};

/** What becomes of the selected faces in By Shape matching. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshExtrudeToPathsInnerFaces : uint8
{
	/**
	 * The selected faces stay, with their rim fitted onto the path: the rim's vertices move onto it and the path's
	 * extra corners are inserted into the rim edges. Interior vertices, UVs and the surface's own shape survive.
	 */
	Keep UMETA(DisplayName="Keep Faces"),
	/**
	 * The selected faces are cut out and replaced by a flat fill of the path. The cap is clean whatever the path
	 * looks like, but has no interior vertices: right for a flat top, wrong for a shaped surface.
	 */
	Refill UMETA(DisplayName="Refill From Path")
};

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
 * new faces, as Extrude Faces and Inset Faces do. The difference is where the separated rim goes: onto the paths,
 * instead of along a direction or by a distance. Whether the result is an inset, an extrude or something in between
 * is decided only by where the paths are.
 *
 * By Shape (the default) treats each path as a closed outline. Each rim loop of the selection is fitted to the path
 * nearest it in shape; the strip is triangulated between the rim and the path whatever their point counts, joining
 * every rim vertex to the run of path corners nearest it. Every path corner becomes a vertex, so the result has
 * exactly the path's shape. Feed it Selection To Paths followed by any path operations - a Clipper2 offset, a
 * boolean, a smooth - with no attribute bookkeeping.
 *
 * By Vertex ID is the older one-for-one mode: each path point names the rim vertex it moves.
 *
 * Requires a selection: with the whole mesh selected there is no rim.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh selection selector extrude inset offset faces path paths rim boundary vertex id outline shape clipper"))
class PCGUTILSDYNMESH_API UPCGDynMeshExtrudeToPathsSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExtrudeToPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	virtual FString GetAdditionalTitleInformation() const override;
	virtual void PostLoad() override;

	/**
	 * How rim and paths are matched. By Shape fits each rim loop to the closest path outline, any point count.
	 * By Vertex ID moves each rim vertex to the point naming it. Nodes saved before By Shape existed load as By Vertex ID.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	EPCGUtilsDynMeshExtrudeToPathsMatching Matching = EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape;

	/** By Shape: whether the selected faces are kept with their rim fitted to the path, or replaced by a flat fill of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths",
		meta=(PCG_Overridable, EditCondition="Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape", EditConditionHides))
	EPCGUtilsDynMeshExtrudeToPathsInnerFaces InnerFaces = EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Keep;

	/**
	 * By Shape: the largest shape distance (mean distance between rim and path outline, both ways) at which a rim loop
	 * and a path still match. A rim loop with no path within it stays where it is; a path with no rim loop is unused.
	 * Both are logged. Zero matches at any distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths",
		meta=(PCG_Overridable, ClampMin="0.0", EditCondition="Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape", EditConditionHides))
	double MaxMatchDistance = 0.0;

	/**
	 * By Vertex ID: point attribute (int32, required on every point of Paths) naming the mesh vertex the point belongs
	 * to. It is what DynMesh | Selection To Paths writes when Output Vertex Index is on, under the same default name.
	 * By Shape reads no attribute.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths",
		meta=(PCG_Overridable, EditCondition="Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByVertexID", EditConditionHides))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/**
	 * The path points are in world space and are brought into the mesh's own space (the PCG target actor's) before
	 * they are used. Leave on when the paths came from Selection To Paths with Output To World Space on, which is
	 * its default. Turn off when they were output mesh-local.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths", meta=(PCG_Overridable))
	bool bPathsInWorldSpace = true;

	/**
	 * What the vertices inside the selected faces do. Faces with no vertex of their own inside are not affected.
	 * Does not apply to Refill From Path, which has no interior vertices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude To Paths",
		meta=(PCG_Overridable, EditCondition="Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByVertexID || InnerFaces == EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Keep", EditConditionHides))
	EPCGUtilsDynMeshExtrudeToPathsInterior InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;

	/** UVs of the new strip faces (and of a refilled cap) are planar-projected and scaled by this. */
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
