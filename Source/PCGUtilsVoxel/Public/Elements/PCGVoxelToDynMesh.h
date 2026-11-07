// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGElement.h"
#include "Voxel/PCGVoxelMesher.h"

#include "PCGVoxelToDynMesh.generated.h"

class UMaterialInterface;

/** Where the material of a voxel mesh's faces comes from. */
UENUM(BlueprintType)
enum class EPCGVoxelMaterialSource : uint8
{
	/** No material slots. */
	None,
	/** One material for every face. */
	Constant,
	/** A point attribute names each cell's material; each distinct value becomes one slot. */
	Attribute,
};

/** Where the vertex colors of a voxel mesh come from. */
UENUM(BlueprintType)
enum class EPCGVoxelColorSource : uint8
{
	/** No vertex colors. */
	None,
	/** A point attribute or property gives each cell's color. */
	Attribute,
};

/** How a voxel mesh's outer surface is offset from the cell faces. */
UENUM(BlueprintType)
enum class EPCGVoxelSurfaceOffset : uint8
{
	/** Faces sit exactly on the cell boundaries. */
	None,
	/** Each cell side moves by how far the point's bounds reach past the cell there. */
	FromPointBounds UMETA(DisplayName="From Point Bounds"),
	/** Every face moves outward by the same distance (negative moves inward). */
	Constant,
};

/** How Atlas From Attribute picks each cell's tile. */
UENUM(BlueprintType)
enum class EPCGVoxelAtlasSource : uint8
{
	/** An integer tile index, counted along rows of Atlas Columns from the top left. */
	TileIndex UMETA(DisplayName="Tile Index"),
	/** A Vector4 UV rectangle: (MinU, MinV, MaxU, MaxV). */
	Rect,
};

/**
 * Builds one DynMesh per voxel set: the closed outer surface of its cells. Faces between two occupied cells are never
 * made. Cells that touch only along an edge or at a corner get separate vertices there, so the mesh stays manifold.
 * A chunked set gives the same mesh as its expanded form.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels mesh to DynMesh dynamic mesh cubes blocks surface greedy meshing vertex color uv atlas"))
class PCGUTILSVOXEL_API UPCGVoxelToDynMeshSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelToDynMeshSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelToDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** None leaves the mesh without material slots; Constant (default) uses Material; Attribute reads Material Attribute per cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable))
	EPCGVoxelMaterialSource MaterialSource = EPCGVoxelMaterialSource::Constant;

	/** Constant only. Empty means the engine's default surface material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable,
		EditCondition="MaterialSource == EPCGVoxelMaterialSource::Constant", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> Material;

	/**
	 * Attribute only. Point attribute naming each cell's material: a soft object path or a string. Required in
	 * Attribute mode. Each distinct value becomes one material slot, in the order first seen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(PCG_Overridable,
		EditCondition="MaterialSource == EPCGVoxelMaterialSource::Attribute", EditConditionHides))
	FName MaterialAttribute = TEXT("Material");

	/**
	 * Merge neighbouring faces that share a plane, material, color, offset and polygroup into one face. Each merged face
	 * keeps every vertex its neighbours use along its edges, so there are no cracks. Ignored in Atlas From Attribute mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(PCG_Overridable))
	bool bCollapseCoplanarFaces = false;

	/**
	 * Moves the outer surface off the cell faces, like Blender's manifold extrude: only the surface moves and it stays
	 * closed. Where neighbouring cells on one flat surface differ, their shared vertices take the average.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(PCG_Overridable))
	EPCGVoxelSurfaceOffset SurfaceOffset = EPCGVoxelSurfaceOffset::None;

	/** Constant only. Distance every face moves outward; negative moves inward. Must be greater than minus half the smallest cell axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(PCG_Overridable,
		EditCondition="SurfaceOffset == EPCGVoxelSurfaceOffset::Constant", EditConditionHides))
	FPCGUtilsInputShorthandDouble SurfaceOffsetDistance{0.0};

	/** None (default) or Attribute: read each cell's color from Color Attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vertex Color", meta=(PCG_Overridable))
	EPCGVoxelColorSource VertexColorSource = EPCGVoxelColorSource::None;

	/** Attribute only. The cell color: a Vector4 (RGBA), or anything that converts to one. Default $Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vertex Color", meta=(PCG_Overridable,
		EditCondition="VertexColorSource == EPCGVoxelColorSource::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector ColorAttribute;

	/** Where neighbouring cells have different colors: Hard (default) keeps each face its cell's color; Smooth blends at shared corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vertex Color", meta=(PCG_Overridable,
		EditCondition="VertexColorSource == EPCGVoxelColorSource::Attribute", EditConditionHides))
	EPCGVoxelColorTransition ColorTransition = EPCGVoxelColorTransition::Hard;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable))
	EPCGVoxelUVMode UVMode = EPCGVoxelUVMode::GridProjection;

	/** Grid Projection only. UV units per cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable,
		EditCondition="UVMode == EPCGVoxelUVMode::GridProjection", EditConditionHides))
	double UVScale = 1.0;

	/** The UV channel written. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable, ClampMin="0", ClampMax="7",
		EditCondition="UVMode != EPCGVoxelUVMode::None", EditConditionHides))
	int32 UVChannel = 0;

	/** Atlas only. Whether Atlas Attribute is a tile index or a UV rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable,
		EditCondition="UVMode == EPCGVoxelUVMode::AtlasFromAttribute", EditConditionHides))
	EPCGVoxelAtlasSource AtlasSource = EPCGVoxelAtlasSource::TileIndex;

	/** Atlas only. Point attribute holding each cell's tile: an integer index (Tile Index) or a Vector4 (Rect). Required in this mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable,
		EditCondition="UVMode == EPCGVoxelUVMode::AtlasFromAttribute", EditConditionHides))
	FName AtlasAttribute = TEXT("AtlasTile");

	/** Tile Index only. Tiles across the atlas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable, ClampMin="1",
		EditCondition="UVMode == EPCGVoxelUVMode::AtlasFromAttribute && AtlasSource == EPCGVoxelAtlasSource::TileIndex", EditConditionHides))
	int32 AtlasColumns = 4;

	/** Tile Index only. Tiles down the atlas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UVs", meta=(PCG_Overridable, ClampMin="1",
		EditCondition="UVMode == EPCGVoxelUVMode::AtlasFromAttribute && AtlasSource == EPCGVoxelAtlasSource::TileIndex", EditConditionHides))
	int32 AtlasRows = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	EPCGVoxelPolygroupMode Polygroups = EPCGVoxelPolygroupMode::PerFaceDirection;

	/** DynMesh data is in the PCG target actor's local space by convention. Off writes world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bConvertToLocalSpace = true;

	/** Copy the input's @Data attributes (Voxel Size, grid transform and anything else) onto the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bCopyDataAttributes = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelToDynMeshElement : public IPCGElement
{
public:
	/** Materials are loaded and the target actor resolved on the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
