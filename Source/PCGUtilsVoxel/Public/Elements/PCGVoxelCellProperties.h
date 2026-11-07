// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelCellProperties.generated.h"

/** Which neighbours join cells into one component. */
UENUM(BlueprintType)
enum class EPCGVoxelConnectivity : uint8
{
	/** Cells sharing a face (6 neighbours). */
	Faces,
	/** Cells sharing a face or an edge (18 neighbours). */
	Edges,
	/** Cells sharing a face, an edge or a corner (26 neighbours). */
	Corners,
};

/** The space Open Normal is written in. */
UENUM(BlueprintType)
enum class EPCGVoxelDirectionSpace : uint8
{
	/** Grid axes: +X is the grid's X, whatever the grid transform's rotation. */
	Grid,
	/** World axes: the grid direction rotated by the grid transform. */
	World,
};

/**
 * Writes per-cell properties of a voxel set as point attributes: which sides are open (next to an empty cell),
 * whether the cell is surrounded, and optional neighbourhood, depth, column and connectivity values. Each output has
 * its own toggle and attribute name. The output is the input with attributes added; a chunked input comes out
 * expanded to one point per cell, because every property is per cell.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels cell properties open sides surrounded neighbors neighbours exposed depth surface island component sky mask"))
class PCGUTILSVOXEL_API UPCGVoxelCellPropertiesSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelCellProperties"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	// ---- Open sides (on by default). A side is open when the face neighbour on that side is empty.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsSurrounded = true;

	/** Optional output, Boolean: true when all six face neighbours are filled (no open side). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteIsSurrounded"))
	FName IsSurroundedAttribute = TEXT("IsSurrounded");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOpenSides = true;

	/** Optional output, Integer32: bitmask of open sides. +X = 1, -X = 2, +Y = 4, -Y = 8, +Z = 16, -Z = 32 (grid axes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteOpenSides"))
	FName OpenSidesAttribute = TEXT("OpenSides");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOpenSideCount = true;

	/** Optional output, Integer32: how many of the six sides are open (0 to 6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteOpenSideCount"))
	FName OpenSideCountAttribute = TEXT("OpenSideCount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePositiveOpenSides = true;

	/** Optional output, Vector: 1 on each grid axis whose + side is open, else 0. For example (0, 0, 1) for an open top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWritePositiveOpenSides"))
	FName PositiveOpenSidesAttribute = TEXT("PositiveOpenSides");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNegativeOpenSides = true;

	/**
	 * Optional output, Vector: -1 on each grid axis whose - side is open, else 0. For example (0, 0, -1) for an open
	 * bottom. Positive + Negative is the sum of the open sides' outward directions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteNegativeOpenSides"))
	FName NegativeOpenSidesAttribute = TEXT("NegativeOpenSides");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOpenNormal = false;

	/**
	 * Optional output, Vector: the normalized sum of the open sides' outward directions, a rough surface normal.
	 * (0, 0, 0) when no side is open or open sides cancel out (both sides of one axis and nothing else).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteOpenNormal"))
	FName OpenNormalAttribute = TEXT("OpenNormal");

	/** The space Open Normal is written in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteOpenNormal"))
	EPCGVoxelDirectionSpace OpenNormalSpace = EPCGVoxelDirectionSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOpenAxisCount = false;

	/** Optional output, Integer32: how many grid axes have at least one open side. 0 inside, 1 on a face, 2 on an edge, 3 on a corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Open Sides", meta=(PCG_Overridable, EditCondition="bWriteOpenAxisCount"))
	FName OpenAxisCountAttribute = TEXT("OpenAxisCount");

	// ---- Neighbourhood.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Neighbourhood", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNeighborCount = false;

	/** Optional output, Integer32: how many of the 26 surrounding cells (faces, edges and corners) are filled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Neighbourhood", meta=(PCG_Overridable, EditCondition="bWriteNeighborCount"))
	FName NeighborCountAttribute = TEXT("NeighborCount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Neighbourhood", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDepth = false;

	/** Optional output, Integer32: face steps to the nearest empty cell. A cell with an open side is 1, the next layer in is 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Neighbourhood", meta=(PCG_Overridable, EditCondition="bWriteDepth"))
	FName DepthAttribute = TEXT("Depth");

	// ---- Column (grid +Z is up).

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Column", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCellsAbove = false;

	/** Optional output, Integer32: filled cells stacked directly above without a gap. 0 for a cell with an open top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Column", meta=(PCG_Overridable, EditCondition="bWriteCellsAbove"))
	FName CellsAboveAttribute = TEXT("CellsAbove");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Column", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsSkyVisible = false;

	/** Optional output, Boolean: true when no filled cell is anywhere above in the cell's grid column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Column", meta=(PCG_Overridable, EditCondition="bWriteIsSkyVisible"))
	FName IsSkyVisibleAttribute = TEXT("IsSkyVisible");

	// ---- Components.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Components", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteComponentID = false;

	/** Optional output, Integer32: which connected component the cell belongs to, numbered from 0 in cell order (Z, then Y, then X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Components", meta=(PCG_Overridable, EditCondition="bWriteComponentID"))
	FName ComponentIDAttribute = TEXT("ComponentID");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Components", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteComponentSize = false;

	/** Optional output, Integer32: how many cells the cell's connected component has. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Components", meta=(PCG_Overridable, EditCondition="bWriteComponentSize"))
	FName ComponentSizeAttribute = TEXT("ComponentSize");

	/** Which neighbours join cells into one component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Components", meta=(PCG_Overridable))
	EPCGVoxelConnectivity Connectivity = EPCGVoxelConnectivity::Faces;

	// ---- Cell.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Cell", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCellIndex = false;

	/** Optional output, Vector: the cell's whole-number grid index (cell centre = index x Voxel Size in grid space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs|Cell", meta=(PCG_Overridable, EditCondition="bWriteCellIndex"))
	FName CellIndexAttribute = TEXT("CellIndex");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelCellPropertiesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
