// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGElement.h"

#include "PCGVoxelOffsetSurface.generated.h"

/** What Amount is measured in. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetUnits : uint8
{
	/** Cells: 1 is one cell width along each axis (so a non-cubic cell moves further along its longer axes). */
	Cells,
	/** World units: the same distance on every axis, turned into cells per axis by Voxel Size. */
	Absolute UMETA(DisplayName="Absolute (World Units)"),
};

/** Where Amount Scale comes from. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetScaleInput : uint8
{
	/** The Amount Scale number on the node (default 1: no change). */
	Constant,
	/** Amount Scale Attribute: a point attribute or property gives one value per cell; an @Data attribute one per set. */
	Attribute,
};

/** What a negative Amount of a cell or more does to the cells it takes away. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetRemovedCells : uint8
{
	/** The outer cells are deleted; the cells behind them become the surface with their own values. */
	Delete,
	/**
	 * The surface is pushed in: the same cells are taken away, but each newly exposed cell takes the values of the
	 * nearest cell of the old surface, so the surface keeps its look (colors, ids) as it sinks.
	 */
	Push,
};

/** Where a cell added by a positive Amount of a cell or more gets its values from. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetAddedValues : uint8
{
	/** Everything from the input cell that grew into it: the surface grows as if its cells were copied outward. */
	Nearest,
	/** Attribute defaults; native properties (color, density, rotation, scale) still come from the cell that grew into it. */
	None,
};

/**
 * Offsets the surface of a voxel set, like adding the open-side vectors to each cell's bounds, but aware of the grid.
 * Each cell's offset is Amount x Amount Scale (in cells or world units). The whole part adds or removes layers of cells;
 * the fraction moves the cell's open faces (written to the point bounds, the point staying on its cell centre). Corners
 * stay square, as in a box offset; with Split Inner Corners a shrink carves inside corners too by splitting those cells
 * into boxes.
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
	UPCGVoxelOffsetSurfaceSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelOffsetSurface"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * How far the surface moves (times Amount Scale), in Units. Positive grows, negative shrinks. The whole part (in
	 * cells) adds or removes that many layers of cells; the rest moves the open faces of the cells that are left.
	 * -1.25 cells: one layer removed, then the new surface pulled in a quarter cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, UIMin="-4", UIMax="4"))
	double Amount = 0.25;

	/** Cells (default): 1 is one cell width. Absolute: world units, the same distance on every axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetUnits Units = EPCGVoxelOffsetUnits::Cells;

	/** Constant (default, 1): the same offset everywhere. Attribute: Amount is multiplied by Amount Scale Attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetScaleInput AmountScaleInput = EPCGVoxelOffsetScaleInput::Constant;

	/** Multiplies Amount everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="AmountScaleInput == EPCGVoxelOffsetScaleInput::Constant", EditConditionHides))
	double AmountScale = 1.0;

	/**
	 * Required when Amount Scale Input is Attribute (a number). A point attribute or property (default 'AmountScale')
	 * gives each cell its own scale, for falloff or masking (0 leaves a cell alone, negative flips it); every cell of a
	 * chunk shares its point's value. An @Data attribute ('@Data.AmountScale') gives one scale for the whole set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="AmountScaleInput == EPCGVoxelOffsetScaleInput::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector AmountScaleAttribute;

	/** Negative offsets of a cell or more: Delete the outer layers, or Push the surface (and its values) inward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetRemovedCells RemovedCells = EPCGVoxelOffsetRemovedCells::Delete;

	/** Positive offsets of a cell or more: where the added cells get their values. */
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
