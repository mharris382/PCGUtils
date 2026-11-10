// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelRasterize.generated.h"

namespace PCGVoxelRasterizePins
{
	const FName Grid = TEXT("Grid");
}

/** What a Rasterize input is. */
UENUM(BlueprintType)
enum class EPCGVoxelRasterizeSource : uint8
{
	/** DynMesh data: a solid (closed) or open mesh, rasterized in 3D. */
	Mesh,
	/**
	 * Point data read as one closed loop: points in order, the last joined back to the first. It is filled in the
	 * grid's XY plane, over the grid layers its points span (plus Cells Below and Cells Above).
	 */
	Path,
};

/** Which cells Rasterize outputs. Each can be turned on by itself; a cell is output when it falls in any that is on. */
UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGVoxelRasterizeModes : uint8
{
	None = 0 UMETA(Hidden),
	/** Cells whose centre is inside the shape. */
	Fill = 1 << 0,
	/** Cells the shape's surface (the mesh's faces, the path's line) passes through. */
	Intersect = 1 << 1,
	/** Cells next to the Intersect cells that are outside the shape and not touched by it: a shell round the shape. */
	Surround = 1 << 2,
};
ENUM_CLASS_FLAGS(EPCGVoxelRasterizeModes)

/** Which cells count as next to each other for Surround. */
UENUM(BlueprintType)
enum class EPCGVoxelRasterizeNeighbors : uint8
{
	/** Cells sharing a face (6 in 3D, 4 for a path). Thinner shell, but it can leave gaps at diagonal corners. */
	Faces,
	/** Cells sharing a face or an edge (18 in 3D; 8 for a path). */
	FacesAndEdges UMETA(DisplayName="Faces And Edges"),
	/** Cells sharing a face, edge or corner (26 in 3D; 8 for a path). The shell has no gaps. */
	All,
};

/**
 * Turns a mesh or a closed path into a voxel set. Modes pick which cells: Fill (inside), Intersect (touched by the
 * surface) and Surround (the shell of cells just outside), each on its own or together.
 *
 * The grid comes from the optional Grid pin (any voxel set: its Voxel Size and grid transform), so the result lines up
 * cell for cell with that set and Voxel | Boolean can cut it out; without it, from Voxel Size and Grid Transform on
 * the node.
 *
 * Out: one voxel set per input, plain cells ordered Z, Y, X, with Voxel Size and the grid transform in @Data and,
 * optionally, which modes each cell falls in. Carries the input's tags.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels voxelize voxelise rasterize rasterise mesh path loop polygon fill solid shell surround intersect grid"))
class PCGUTILSVOXEL_API UPCGVoxelRasterizeSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelRasterize"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Mesh reads DynMesh inputs; Path reads point inputs as closed loops. An input of the other type is an error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelRasterizeSource Source = EPCGVoxelRasterizeSource::Mesh;

	/** Fill: inside. Intersect: touched by the surface. Surround: the shell just outside. Any mix; at least one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelRasterizeModes"))
	uint8 Modes = static_cast<uint8>(EPCGVoxelRasterizeModes::Fill | EPCGVoxelRasterizeModes::Intersect);

	/**
	 * How far, as a fraction of a cell, the surface may pass outside a cell and still count as touching it (Intersect).
	 * 0 (default): it must pass through the cell; merely lying on a cell's face does not count, so a face that sits
	 * exactly on the grid touches one layer, not two. Negative: it must reach that far into the cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="-0.49", ClampMax="1"))
	double TouchMargin = 0.0;

	/** How many cells thick the Surround shell is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1", ClampMax="64"))
	int32 SurroundCells = 1;

	/** Which cells count as next to each other when growing the Surround shell. All leaves no gaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelRasterizeNeighbors SurroundNeighbors = EPCGVoxelRasterizeNeighbors::All;

	/** Path: extra grid layers below the lowest layer its points sit in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(PCG_Overridable, ClampMin="0"))
	int32 CellsBelow = 0;

	/** Path: extra grid layers above the highest layer its points sit in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(PCG_Overridable, ClampMin="0"))
	int32 CellsAbove = 0;

	/** Mesh: DynMesh data is in the PCG target actor's local space by convention. Off reads the mesh as world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(PCG_Overridable))
	bool bMeshInLocalSpace = true;

	/**
	 * Cell size of the new grid when the Grid pin has no data. With data on Grid, its Voxel Size is used instead (read
	 * through Grid > Voxel Size, @Data.VoxelSize by default).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="New Grid", meta=(PCG_Overridable))
	FVector VoxelSize = FVector(100.0);

	/**
	 * Where the new grid's origin cell centre is and how it is turned, when the Grid pin has no data (scale is ignored).
	 * Identity puts cell centres at whole multiples of Voxel Size in world space, as the Volume Sampler does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="New Grid", meta=(PCG_Overridable))
	FTransform GridTransform = FTransform::Identity;

	/** Write which modes each cell falls in (Integer32 bits: 1 Fill, 2 Intersect, 4 Surround) to Mode Attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteModeAttribute = true;

	/**
	 * Optional output (Integer32): the modes a cell falls in, as bits (1 Fill, 2 Intersect, 4 Surround). Fill and
	 * Intersect bits are set whether or not those modes are on (a surface cell whose centre is inside is 3); the
	 * Surround bit only when Surround is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, EditCondition="bWriteModeAttribute"))
	FName ModeAttribute = TEXT("RasterMode");

	/** The node stops with an error when the box of cells it would scan (the shape's bounds plus the shell) has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelRasterizeElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
