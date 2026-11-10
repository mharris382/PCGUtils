// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelOffsetSurface.generated.h"

/** What a negative Amount of a cell or more does to the cells it takes away. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetRemovedCells : uint8
{
	/** The outer cells are deleted; the cells behind them become the surface with their own values. */
	Delete,
	/**
	 * The surface is pushed in: the same cells are taken away, but each cell of the new surface takes the values of
	 * the nearest cell of the old surface, so the surface keeps its look (colors, ids) as it sinks.
	 */
	Push,
};

/** Where a cell added by a positive Amount of a cell or more gets its values from. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetAddedValues : uint8
{
	/** Everything from the nearest input cell (by centre distance): the surface grows as if its cells were copied outward. */
	Nearest,
	/** Attribute defaults; native properties (color, density, rotation, scale) still come from the nearest input cell. */
	None,
};

/**
 * Offsets the surface of a voxel set by Amount cells, like adding the open-side vectors to each cell's bounds, but
 * aware of the grid. The whole part of Amount adds or removes layers of cells; the fraction moves each cell's open
 * faces by that part of a cell (written to the point bounds, the point staying on its cell centre). Corners stay square,
 * as in a box offset: a positive Amount grows corner cells on every open side and a negative one, with Split Inner
 * Corners, carves the inside corners too by splitting those cells into boxes.
 *
 * Out: In, mutated: one point per cell (a chunked set is expanded), its metadata, tags and @Data kept. Split cells
 * become several points on the same cell; with them, Out is no longer a valid voxel set for other Voxel | nodes.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels offset surface grow shrink inset outset erode dilate thicken thin bounds open sides push corner split"))
class PCGUTILSVOXEL_API UPCGVoxelOffsetSurfaceSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelOffsetSurface"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * How far the surface moves, in cells (1 = one cell width along each axis). Positive grows, negative shrinks.
	 * The whole part adds or removes that many layers of cells; the rest moves the open faces of the cells that are left.
	 * -1.25: one layer removed, then the new surface pulled in a quarter cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, UIMin="-4", UIMax="4"))
	double Amount = 0.25;

	/** Negative Amount of a cell or more: Delete the outer layers, or Push the surface (and its values) inward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetRemovedCells RemovedCells = EPCGVoxelOffsetRemovedCells::Delete;

	/** Positive Amount of a cell or more: where the added cells get their values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetAddedValues AddedValues = EPCGVoxelOffsetAddedValues::Nearest;

	/**
	 * Negative fractions: carve inside corners too. A cell in an inside corner (or under a notch) has no open side
	 * facing the corner, so shrinking open sides alone leaves it sticking out; with this on it is split into the fewest
	 * boxes (up to 3 for one corner) that leave the notch empty. Off: one box per cell, open sides only. Positive
	 * fractions never need splitting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bSplitInnerCorners = true;

	/** The node stops with an error when the box of cells it works over (the set's bounds, plus the added layers) has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelOffsetSurfaceElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
