// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelSlice.generated.h"

/** One grid axis. */
UENUM(BlueprintType)
enum class EPCGVoxelSliceAxis : uint8
{
	X,
	Y,
	Z,
};

/** Where slice boundaries are counted from. */
UENUM(BlueprintType)
enum class EPCGVoxelSliceStart : uint8
{
	/** The set's lowest cell along the axis: slice 0 is the bottom, indices count up from there. */
	SetBounds UMETA(DisplayName="Set Bounds"),
	/**
	 * The grid origin cell: boundaries fall on multiples of Thickness in cell index, and indices can be negative.
	 * After Separate Islands with Re-anchor Grid, slice 0 is the one holding each island's centre cell.
	 */
	GridOrigin UMETA(DisplayName="Grid Origin"),
};

/**
 * Cuts each voxel set into slabs (slices) across one grid axis: one output set per slice, each holding the input's
 * points that fall in it, on the input's grid. A chunk that crosses a boundary is split there; each piece is a copy of
 * the chunk (all its attributes) with its span, position and bounds cut to the slice, keeping the chunk's surface
 * offsets on its outer sides. Every slice keeps the input's grid transform, so after Separate Islands every slice of
 * a column turns about the column's centre in Transform Grid (Pivot = Location, Grid space, 0).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels slice slices slab layers layer cut split section twist spiral column floors"))
class PCGUTILSVOXEL_API UPCGVoxelSliceSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelSlice"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** The grid axis the slices are stacked along (each slice is a slab across it). Z (default) makes floors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelSliceAxis Axis = EPCGVoxelSliceAxis::Z;

	/** How many cells thick each slice is along Axis. Must be at least 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int32 Thickness = 1;

	/** Where the boundaries are counted from: the set's lowest cell (default) or the grid origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelSliceStart Start = EPCGVoxelSliceStart::SetBounds;

	/**
	 * When the input carries the Grid > Surrounded Attribute: on (default), it is rewritten for each slice on its own,
	 * so faces on the cuts count as open (slices are usually turned or moved apart). Off: rewritten against the whole
	 * input set, so cut faces stay hidden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bCutFacesAreOpen = true;

	// ---- Outputs, written to each slice's @Data.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSliceIndex = true;

	/**
	 * Optional output (on by default), @Data Integer32: which slice this is, counted from Start (from 0 at the set's
	 * bottom with Set Bounds; can be negative with Grid Origin). Slices with no cells are not output, but still counted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteSliceIndex"))
	FName SliceIndexAttribute = TEXT("SliceIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSliceFraction = true;

	/**
	 * Optional output (on by default), @Data Double: where the slice sits among its input set's slices, 0 for the
	 * lowest to 1 for the highest (0 when there is only one). Multiply it by a total angle for a twist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteSliceFraction"))
	FName SliceFractionAttribute = TEXT("SliceFraction");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSliceCount = false;

	/** Optional output, @Data Integer32: how many slices its input set spans (lowest to highest index, empty ones included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteSliceCount"))
	FName SliceCountAttribute = TEXT("SliceCount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSliceDistance = false;

	/**
	 * Optional output, @Data Double: the slice's middle along Axis, measured from the grid origin in world units (cell
	 * index times Voxel Size). The same height gives the same value across sets that share a grid, so a twist driven
	 * by it lines up between columns.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteSliceDistance"))
	FName SliceDistanceAttribute = TEXT("SliceDistance");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelSliceElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
