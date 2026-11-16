// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGElement.h"
#include "Voxel/PCGVoxelChunking.h"

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

/** Where Amount Scale came from before it became a point shorthand. Kept only to load older graphs. */
UENUM()
enum class EPCGVoxelOffsetScaleInput : uint8
{
	Constant,
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

/** What Min Thickness does with a run of cells a shrink would leave too thin. */
UENUM(BlueprintType)
enum class EPCGVoxelOffsetThinResult : uint8
{
	/** Shrink less: the run's end faces stop where the run is Min Thickness (or not at all, if it is thinner than that). */
	Clamp,
	/** Delete the run's cells (those with a Min Thickness above 0). */
	Remove,
};

/**
 * Offsets the surface of a voxel set, like adding the open-side vectors to each cell's bounds, but aware of the grid.
 * Each cell's offset is Amount x Amount Scale (in cells or world units); each is a constant or an attribute (one
 * value per point, so per cell, or one @Data value per set). The whole part adds or removes layers of cells;
 * the fraction is a part cell (written to the point bounds, the point staying on its cell centre). No box ever leaves
 * its cell: growing 3.5 adds three layers of cells, then a layer of cells half deep. A grown cell's reach is its box
 * grown by its amount (a box offset, so corners stay square), and a cell reached by several keeps the union of their
 * parts, each copying its own grower. A shrink moves the open faces in; with Split Inner Corners it carves inside
 * corners too. Axes limits the offset to some grid axes (X | Y leaves Z alone).
 *
 * Out: In, mutated and still a valid voxel set: one point per box (a chunked set is expanded), metadata, tags and
 * @Data kept. A cell that takes more than one box (an inside corner grown, or carved on a shrink) is written as several
 * points on one cell (voxel nodes read the cell as filled), each flagged with Was Split Attribute.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels offset surface grow shrink inset outset erode dilate thicken thin bounds open sides push corner split"))
class PCGUTILSVOXEL_API UPCGVoxelOffsetSurfaceSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelOffsetSurfaceSettings();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelOffsetSurface"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * How far the surface moves (times Amount Scale), in Units. Positive grows, negative shrinks. The whole part (in
	 * cells) adds or removes that many layers of cells; the rest is a part cell. 3.5 cells: three layers added, then a
	 * layer of cells half deep (each box stays inside its cell). -1.25 cells: one layer removed, then the open faces of
	 * the cells left pulled in a quarter cell.
	 * Attribute (a number, required in that mode; default 'OffsetAmount'): a point attribute or property gives each
	 * cell its own amount (every cell of a chunk shares its point's value); '@Data.OffsetAmount' gives one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDouble Amount = FPCGUtilsPointShorthandDouble(0.25, TEXT("OffsetAmount"));

	/**
	 * Which grid directions the offset moves along (default all six). Each sign of an axis is its own flag: -Z without
	 * +Z offsets the bottom faces and leaves the tops where they are; +X, -X, +Y and -Y offsets sideways only. A
	 * direction left out adds or removes no layers that way, and its faces do not move. None leaves every cell as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelDirections"))
	int32 Directions = static_cast<int32>(EPCGVoxelDirections::All);

	/** Cells (default): 1 is one cell width. Absolute: world units, the same distance on every axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetUnits Units = EPCGVoxelOffsetUnits::Cells;

	/**
	 * Multiplies Amount per axis, for falloff, masking or exaggeration: (1, 1, 0) leaves a cell's top and bottom where
	 * they are, (1, 1, 2) moves them twice as far as the sides, 0 on every axis leaves the cell alone, and a negative
	 * axis flips the offset along that axis (a cell can grow sideways and shrink vertically). Default constant (1, 1, 1).
	 * Attribute (a vector, or a number used on all three axes; required in that mode; default 'AmountScale'): a point
	 * attribute or property gives each cell its own scale (every cell of a chunk shares its point's value);
	 * '@Data.AmountScale' gives one per set. Graphs saved with a number scale load it on all three axes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandVector AmountScale = FPCGUtilsPointShorthandVector(FVector(1.0), TEXT("AmountScale"));

	/** Negative offsets of a cell or more: Delete the outer layers, or Push the surface (and its values) inward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetRemovedCells RemovedCells = EPCGVoxelOffsetRemovedCells::Delete;

	/** Positive offsets of a cell or more: where the added cells get their values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetAddedValues AddedValues = EPCGVoxelOffsetAddedValues::Nearest;

	/**
	 * Negative fractions: carve inside corners too. A cell in an inside corner (or under a notch) has no open side
	 * facing the corner, so shrinking open sides alone leaves it sticking out; with this on it is split into the fewest
	 * boxes (up to 3 for one corner) that leave the notch empty. Off: one box per cell, open sides only. Growing needs
	 * no setting: a grown inside corner is always the union of its walls' parts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bSplitInnerCorners = true;

	/**
	 * Shrinking: keep the offset from leaving slivers. Along each axis, a run of whole cells whose end faces a shrink
	 * moves in may not end up thinner than Min Thickness; Thin Result says what happens to a run that would. Only the
	 * shrink's fraction is limited (whole layers removed stay removed), and only runs the fraction made thinner count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thinness", meta=(PCG_Overridable))
	bool bLimitThinness = false;

	/**
	 * The thinnest a run of cells may end up along an axis, in Units (Cells: 0.25 is a quarter cell). Attribute (a
	 * number of 0 or more, required in that mode; default 'MinThickness'): a point attribute or property gives each cell
	 * its own minimum (0 = this cell is never clamped or removed, the per-cell switch); '@Data.MinThickness' gives one
	 * per set. A run takes the largest minimum of its cells; only end cells above 0 are clamped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thinness", meta=(PCG_Overridable, EditCondition="bLimitThinness"))
	FPCGUtilsPointShorthandDoubleAbs MinThickness = FPCGUtilsPointShorthandDoubleAbs(0.25, TEXT("MinThickness"));

	/** Clamp (default) shrinks a thin run less; Remove deletes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thinness", meta=(PCG_Overridable, EditCondition="bLimitThinness"))
	EPCGVoxelOffsetThinResult ThinResult = EPCGVoxelOffsetThinResult::Clamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteWasClamped = true;

	/**
	 * Optional output, Boolean (written when Limit Thinness is on): true on each point of a cell Min Thickness clamped
	 * (its shrink limited on some axis); false on every other point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteWasClamped"))
	FName WasClampedAttribute = TEXT("WasClamped");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteWasSplit = true;

	/**
	 * Optional output, Boolean: true on each point of a cell written as several boxes (an inside corner grown, or
	 * carved with Split Inner Corners) and on a cell carved to one box; false on every other point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteWasSplit"))
	FName WasSplitAttribute = TEXT("WasSplit");

	/**
	 * Write each growing cell's added whole cells as chunks (one point per box of cells, its size in the Span Attribute)
	 * instead of a point per cell: +3 on one cell adds a handful of boxes, not 342 points. Cells added by one cell copy
	 * it, so nothing is lost. Part cells (the fraction) join only with part cells of exactly their shape, along the axes
	 * they fill; the input's own cells and split corners stay single points. Voxel | Expand turns chunks back into cells.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable))
	bool bCompressAdded = false;

	/**
	 * Join every whole cell of the output (the input's own and the added) with its equal neighbours into chunks, by the
	 * same rules as Voxel | Compress (Match Attributes). Listed with only the offset attribute joins cells by offset
	 * value; values not listed are then taken from one cell of each chunk. Part cells join as in Compress Added; split
	 * corners stay single points.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable))
	bool bCompressEqualCells = false;

	/** Compress Equal Cells: which values cells must share to join. All loses nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, EditCondition="bCompressEqualCells"))
	EPCGVoxelMatchAttributes MatchAttributes = EPCGVoxelMatchAttributes::All;

	/**
	 * Compress Equal Cells with Match Attributes Listed: the values cells must share. Point attribute names, or $Color,
	 * $Density, $Steepness. Each must exist on the input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, EditCondition="bCompressEqualCells && MatchAttributes == EPCGVoxelMatchAttributes::Listed"))
	TArray<FName> ListedAttributes;

	/** The largest chunk along any axis, in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, ClampMin=1, UIMin=1, UIMax=64))
	int32 CompressMaxSpan = 16;

	/** The node stops with an error when the box of cells it works over (the set's bounds, plus the added layers) has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	/** Older graphs: the axes the offset moved along (an EPCGVoxelAxes mask), now both directions of each in Directions. */
	UPROPERTY()
	int32 Axes = static_cast<int32>(EPCGVoxelAxes::All);

	/** Older graphs: Constant or Attribute for Amount Scale, now AmountScale.Input. */
	UPROPERTY()
	EPCGVoxelOffsetScaleInput AmountScaleInput_DEPRECATED = EPCGVoxelOffsetScaleInput::Constant;

	/** Older graphs: the Amount Scale attribute, now AmountScale.Attribute. */
	UPROPERTY()
	FPCGAttributePropertyInputSelector AmountScaleAttribute_DEPRECATED;

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelOffsetSurfaceElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
