// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelSeparateIslands.generated.h"

/**
 * Splits each voxel set into its connected parts (islands): one output set per island, holding that island's points
 * unchanged (chunks stay chunks) with the input's metadata, @Data and tags. Optionally re-anchors each island's grid at
 * a cell of its own (the centre by default), so the island's VoxelGridTransform origin is its pivot: Transform Grid
 * with Pivot = Location, Space = Grid and Pivot Location 0 then turns each island about its own centre, and any set cut
 * from an island later (slices of a column) still turns about the island's centre.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels separate split islands island connected components parts pieces partition pivot anchor"))
class PCGUTILSVOXEL_API UPCGVoxelSeparateIslandsSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelSeparateIslandsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelSeparateIslands"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Which neighbours join cells into one island. Faces (default): cells touching only along an edge or at a corner are separate islands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelConnectivity Connectivity = EPCGVoxelConnectivity::Faces;

	/** Islands with fewer cells than this are dropped (not output). 1 keeps every island; values below 1 act as 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int32 MinCells = 1;

	/**
	 * Moves each island's grid origin to a cell of the island (chosen by Anchor Alignment) and writes it to the Grid
	 * Transform Attribute. No point moves; only which cell is the origin changes. Off: every island keeps the input's
	 * grid transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor", meta=(PCG_Overridable))
	bool bReanchorGrid = true;

	/**
	 * Where the new origin sits in the island's cell bounds, per grid axis: 0 = min side, 0.5 = centre, 1 = max side
	 * (values outside 0-1 put it outside the bounds). The origin must be a cell centre, so it is the cell nearest that
	 * spot: on an axis where the island is an even number of cells wide, the centre is half a cell off (rounded toward
	 * the max side). @Data mode reads a Vector (default @Data.AnchorAlignment) from each input set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anchor", meta=(PCG_Overridable, EditCondition="bReanchorGrid"))
	FPCGUtilsInputShorthandVector AnchorAlignment;

	// ---- Outputs, written to each island's @Data.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIslandIndex = true;

	/**
	 * Optional output (on by default), @Data Integer32: the island's number within its input set, from 0, in cell order
	 * (Z, then Y, then X, by each island's lowest cell). Dropped islands are not counted, so numbers have no gaps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteIslandIndex"))
	FName IslandIndexAttribute = TEXT("IslandIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIslandCount = false;

	/** Optional output, @Data Integer32: how many islands its input set gave (after Min Cells), for spreading values over islands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteIslandCount"))
	FName IslandCountAttribute = TEXT("IslandCount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCellCount = false;

	/** Optional output, @Data Integer32: how many cells the island has. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteCellCount"))
	FName CellCountAttribute = TEXT("IslandCellCount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsCenter = false;

	/**
	 * Optional output, @Data Vector: the world position of the exact centre of the island's cell bounds (not snapped to
	 * a cell, unlike the re-anchored origin).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteBoundsCenter"))
	FName BoundsCenterAttribute = TEXT("IslandCenter");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelSeparateIslandsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
