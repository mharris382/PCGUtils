// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsDynMeshProperties.generated.h"

class UPCGDynamicMeshData;
struct FPCGContext;

/**
 * Mesh properties that cost nothing to read: counts the mesh already tracks and attribute-layer layout. Each one
 * is written to the @Data domain under its own name, only when its flag is on.
 *
 * Shared by DynMesh | Properties and by topology-changing nodes (Simplify, Remesh, Self Union, the extrude family,
 * and so on) that can write these inline after they run. Default names match across all of them, so a renamed
 * write before a node and a default-named write after it can be subtracted directly.
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshFreeProperties
{
	GENERATED_BODY()

	/** Write the triangle count. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputTriangleCount = false;

	/** @Data attribute for the triangle count. int32, optional, written only when Output Triangle Count is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputTriangleCount", EditConditionHides))
	FName TriangleCountAttributeName = TEXT("TriangleCount");

	/** Write the vertex count. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputVertexCount = false;

	/** @Data attribute for the vertex count. int32, optional, written only when Output Vertex Count is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputVertexCount", EditConditionHides))
	FName VertexCountAttributeName = TEXT("VertexCount");

	/** Write the edge count. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputEdgeCount = false;

	/** @Data attribute for the edge count. int32, optional, written only when Output Edge Count is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputEdgeCount", EditConditionHides))
	FName EdgeCountAttributeName = TEXT("EdgeCount");

	/**
	 * Write the Euler characteristic, vertices - edges + triangles. int32. A closed mesh with no holes through it
	 * gives 2 per piece; each handle (a hole through the solid) lowers it by 2. A change across a simplify means
	 * topology changed, not just density.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputEulerCharacteristic = false;

	/** @Data attribute for the Euler characteristic. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputEulerCharacteristic", EditConditionHides))
	FName EulerCharacteristicAttributeName = TEXT("EulerCharacteristic");

	/** Write whether vertex and triangle IDs are dense (no gaps left by deletions). bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputIsCompact = false;

	/** @Data attribute for compactness. bool, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputIsCompact", EditConditionHides))
	FName IsCompactAttributeName = TEXT("IsCompact");

	/** Write the number of material slots on the data. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputMaterialSlotCount = false;

	/** @Data attribute for the material slot count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputMaterialSlotCount", EditConditionHides))
	FName MaterialSlotCountAttributeName = TEXT("MaterialSlotCount");

	/**
	 * Write the material in each slot, as soft object paths. With Max Material Count above 1, the slot index is
	 * appended to the name (Material0, Material1, ...); slots past the data's last are written empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputMaterials = false;

	/** @Data attribute (or base name) for slot materials. Soft object path, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputMaterials", EditConditionHides))
	FName MaterialAttributeName = TEXT("Material");

	/** How many slots to write. 1 writes slot 0 under the name as given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, ClampMin="1", UIMin="1", EditCondition="bOutputMaterials", EditConditionHides))
	int32 MaxMaterialCount = 1;

	/** Write the number of UV channels. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputUVChannelCount = false;

	/** @Data attribute for the UV channel count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputUVChannelCount", EditConditionHides))
	FName UVChannelCountAttributeName = TEXT("UVChannelCount");

	/** Write whether the mesh has a normals layer. bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputHasNormals = false;

	/** @Data attribute for the normals-layer flag. bool, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputHasNormals", EditConditionHides))
	FName HasNormalsAttributeName = TEXT("HasNormals");

	/** Write whether the mesh has a vertex color layer. bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputHasVertexColors = false;

	/** @Data attribute for the vertex-color-layer flag. bool, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputHasVertexColors", EditConditionHides))
	FName HasVertexColorsAttributeName = TEXT("HasVertexColors");

	/** Write whether the mesh has default-layer PolyGroups. bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputHasPolyGroups = false;

	/** @Data attribute for the default PolyGroup flag. bool, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputHasPolyGroups", EditConditionHides))
	FName HasPolyGroupsAttributeName = TEXT("HasPolyGroups");

	/** Write the number of extended (named) PolyGroup layers, not counting the default layer. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable))
	bool bOutputPolyGroupLayerCount = false;

	/** @Data attribute for the extended PolyGroup layer count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, EditCondition="bOutputPolyGroupLayerCount", EditConditionHides))
	FName PolyGroupLayerCountAttributeName = TEXT("PolyGroupLayerCount");

	bool AnyEnabled() const;
};

/**
 * Mesh properties that need a pass over the mesh (linear in its size). Written to the @Data domain, each under
 * its own name, only when its flag is on; nothing is computed for a flag that is off. Values are in the mesh's own
 * space (normally the PCG target actor's local space).
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshComputedProperties
{
	GENERATED_BODY()

	/** Write the minimum corner of the mesh's bounding box. FVector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputBoundsMin = false;

	/** @Data attribute for the bounds minimum. FVector, optional. Zero for an empty mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputBoundsMin", EditConditionHides))
	FName BoundsMinAttributeName = TEXT("BoundsMin");

	/** Write the maximum corner of the mesh's bounding box. FVector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputBoundsMax = false;

	/** @Data attribute for the bounds maximum. FVector, optional. Zero for an empty mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputBoundsMax", EditConditionHides))
	FName BoundsMaxAttributeName = TEXT("BoundsMax");

	/** Write the total triangle area. double, square units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputSurfaceArea = false;

	/** @Data attribute for the surface area. double, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputSurfaceArea", EditConditionHides))
	FName SurfaceAreaAttributeName = TEXT("SurfaceArea");

	/** Write the enclosed volume. double, cubic units. Only meaningful for a closed mesh; negative if it is inside out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputVolume = false;

	/** @Data attribute for the volume. double, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputVolume", EditConditionHides))
	FName VolumeAttributeName = TEXT("Volume");

	/** Write whether the mesh has no open (boundary) edges. bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputIsClosed = false;

	/** @Data attribute for closedness. bool, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputIsClosed", EditConditionHides))
	FName IsClosedAttributeName = TEXT("IsClosed");

	/** Write the number of open edges (edges with one triangle). int32. Holes and cracks show up here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputBoundaryEdgeCount = false;

	/** @Data attribute for the open edge count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputBoundaryEdgeCount", EditConditionHides))
	FName BoundaryEdgeCountAttributeName = TEXT("BoundaryEdgeCount");

	/** Write the number of separate connected pieces. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputConnectedComponentCount = false;

	/** @Data attribute for the piece count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputConnectedComponentCount", EditConditionHides))
	FName ConnectedComponentCountAttributeName = TEXT("ConnectedComponentCount");

	/** Write the number of distinct default-layer PolyGroup IDs. int32. 0 when the mesh has no PolyGroups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable))
	bool bOutputPolyGroupCount = false;

	/** @Data attribute for the PolyGroup count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, EditCondition="bOutputPolyGroupCount", EditConditionHides))
	FName PolyGroupCountAttributeName = TEXT("PolyGroupCount");

	/** Write the number of triangles with area at or below Degenerate Area Threshold. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable))
	bool bOutputDegenerateTriangleCount = false;

	/** @Data attribute for the degenerate triangle count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, EditCondition="bOutputDegenerateTriangleCount", EditConditionHides))
	FName DegenerateTriangleCountAttributeName = TEXT("DegenerateTriangleCount");

	/** Triangles with this area or less, in square units, count as degenerate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, ClampMin="0.0", EditCondition="bOutputDegenerateTriangleCount", EditConditionHides))
	double DegenerateAreaThreshold = 1.0e-4;

	/** Write the smallest corner angle of any non-degenerate triangle, in degrees. double. Slivers drive it toward 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable))
	bool bOutputMinTriangleAngle = false;

	/** @Data attribute for the minimum triangle angle. double, optional. 0 for an empty mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, EditCondition="bOutputMinTriangleAngle", EditConditionHides))
	FName MinTriangleAngleAttributeName = TEXT("MinTriangleAngle");

	/**
	 * Write the number of triangles whose facing disagrees with their normals layer: the geometric face normal
	 * points away from the average of the triangle's three stored normals. These render dark or inside-out, and
	 * are the usual sign of a fold-over or of normals left stale by a topology change. int32; 0 with no normals layer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable))
	bool bOutputInvertedNormalTriangleCount = false;

	/** @Data attribute for the inverted-normal triangle count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, EditCondition="bOutputInvertedNormalTriangleCount", EditConditionHides))
	FName InvertedNormalTriangleCountAttributeName = TEXT("InvertedNormalTriangleCount");

	/** Write the number of edges where the normals layer splits (hard edges). int32; 0 with no normals layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable))
	bool bOutputNormalSeamEdgeCount = false;

	/** @Data attribute for the hard-edge count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, EditCondition="bOutputNormalSeamEdgeCount", EditConditionHides))
	FName NormalSeamEdgeCountAttributeName = TEXT("NormalSeamEdgeCount");

	/** Write the number of bowtie vertices (two fans of triangles meeting at one vertex): non-manifold points. int32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable))
	bool bOutputBowtieVertexCount = false;

	/** @Data attribute for the bowtie vertex count. int32, optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed|Quality", meta=(PCG_Overridable, EditCondition="bOutputBowtieVertexCount", EditConditionHides))
	FName BowtieVertexCountAttributeName = TEXT("BowtieVertexCount");

	bool AnyEnabled() const;
};

/**
 * Reads mesh properties and writes them to a DynMesh data's @Data domain. Use these from any node that wants to
 * report what it produced; DynMesh | Properties is a thin wrapper around them.
 */
namespace PCGUtilsDynMeshProperties
{
	/**
	 * Checks every enabled property's attribute name. An enabled property with a None name is a graph warning and
	 * is skipped later; a name PCG cannot use is a graph error. Each message names the property and the value.
	 * Returns false only on an error.
	 */
	PCGUTILSDYNMESH_API bool Validate(const FPCGUtilsDynMeshFreeProperties& Properties, FPCGContext* Context);
	PCGUTILSDYNMESH_API bool Validate(const FPCGUtilsDynMeshComputedProperties& Properties, FPCGContext* Context);

	/** Writes every enabled free property of Data's mesh to its @Data domain. */
	PCGUTILSDYNMESH_API void Write(UPCGDynamicMeshData& Data, const FPCGUtilsDynMeshFreeProperties& Properties);

	/** Computes, in as few passes as possible, and writes every enabled computed property. */
	PCGUTILSDYNMESH_API void Write(UPCGDynamicMeshData& Data, const FPCGUtilsDynMeshComputedProperties& Properties);
}
