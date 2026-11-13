// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/PCGVoxelOffsetSurface.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelExtrude.generated.h"

/**
 * Grows or cuts each cell along a direction. Each cell's steps per axis are |Direction| x |Amount| on that axis,
 * rounded up to a whole number of cells (1.2 gives 2), towards the sign of Direction on that axis. A positive Amount
 * adds that many cells beyond the cell; a negative Amount removes the cell itself and that many cells beyond it
 * (-1: the cell and its neighbour in the direction). 0 does nothing, so a cell never removes only itself.
 *
 * Separate Axes (default) grows each axis on its own, as up to three straight runs; off grows the whole box the
 * steps span. One call grows one way per axis, so it never grows up and down at once.
 *
 * Out: In, mutated and still a valid voxel set: one point per cell (a chunked set is expanded), metadata, tags and
 * @Data kept. Added cells copy the cell they grew from (Added Values) with whole-cell bounds; cells already filled
 * stay as they are; removals win over additions. Is Surrounded is rewritten.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels extrude grow extend stretch push pull remove carve direction column pillar"))
class PCGUTILSVOXEL_API UPCGVoxelExtrudeSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelExtrude"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Which way, and how far per unit of Amount, each cell grows, in grid axes. (0, 0, 1) grows up; (3, 0, 0) grows
	 * three cells along +X; (1, 0, -1) grows along +X and down. Each axis is rounded up to whole cells after multiplying
	 * by Amount. (0, 0, 0) does nothing.
	 * Attribute (a vector, required in that mode; default 'ExtrudeDirection'): a point attribute or property gives each
	 * cell its own direction (every cell of a chunk shares its point's value); '@Data.ExtrudeDirection' gives one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandVector Direction = FPCGUtilsPointShorthandVector(FVector(0.0, 0.0, 1.0), TEXT("ExtrudeDirection"));

	/**
	 * Multiplies Direction. Positive adds cells beyond the cell; negative removes the cell and cells beyond it; 0 does
	 * nothing. Whole numbers are used as they are; a fraction is rounded up to the next whole cell (away from zero).
	 * Attribute (a number, required in that mode; default 'ExtrudeAmount'): a point attribute or property gives each
	 * cell its own amount; '@Data.ExtrudeAmount' gives one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDouble Amount = FPCGUtilsPointShorthandDouble(1.0, TEXT("ExtrudeAmount"));

	/**
	 * On (default): each axis grows on its own, a straight run per axis (an L or a cross of runs, never the space
	 * between them). Off: the whole box between the cell and its steps on every axis is grown (or removed).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bSeparateAxes = true;

	/** Where an added cell gets its values: Nearest copies the cell it grew from; None uses attribute defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetAddedValues AddedValues = EPCGVoxelOffsetAddedValues::Nearest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteExtrudeStep = false;

	/**
	 * Optional output, Integer32: how many steps out each added cell is (1 for the first cell grown, 2 for the next,
	 * ...). Every cell that was already there, including the cells that grew, gets 0. With Separate Axes off a cell's
	 * step is its largest per-axis distance. Where several cells grow into one, the smallest step wins. Only positive
	 * extrudes write steps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteExtrudeStep"))
	FName ExtrudeStepAttribute = TEXT("ExtrudeStep");

	/** The node stops with an error on a set whose cells would add or remove more cells than this in total. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelExtrudeElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
