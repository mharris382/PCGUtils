// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGElement.h"

#include "PCGVoxelOffsetSurface.generated.h"

/** Grid axes, as flags. */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGVoxelAxes : uint8
{
	None = 0 UMETA(Hidden),
	X = 1 << 0,
	Y = 1 << 1,
	Z = 1 << 2,
	All = X | Y | Z UMETA(Hidden),
};
ENUM_CLASS_FLAGS(EPCGVoxelAxes);

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

/**
 * Offsets the surface of a voxel set, like adding the open-side vectors to each cell's bounds, but aware of the grid.
 * Each cell's offset is Amount x Amount Scale (in cells or world units); each is a constant or an attribute (one
 * value per point, so per cell, or one @Data value per set). The whole part adds or removes layers of cells;
 * the fraction moves the cell's open faces (written to the point bounds, the point staying on its cell centre). Corners
 * stay square, as in a box offset; with Split Inner Corners a shrink carves inside corners too by splitting those cells
 * into boxes. Axes limits the offset to some grid axes (X | Y leaves Z alone).
 *
 * Out: In, mutated and still a valid voxel set: one point per box (a chunked set is expanded), metadata, tags and
 * @Data kept, bounds moved on the open faces. A cell carved at an inside corner is written as its carved boxes, several
 * points on one cell (voxel nodes read the cell as filled; meshing reads the box around them), each flagged with Was
 * Split Attribute.
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
	 * cells) adds or removes that many layers of cells; the rest moves the open faces of the cells that are left.
	 * -1.25 cells: one layer removed, then the new surface pulled in a quarter cell.
	 * Attribute (a number, required in that mode; default 'OffsetAmount'): a point attribute or property gives each
	 * cell its own amount (every cell of a chunk shares its point's value); '@Data.OffsetAmount' gives one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDouble Amount = FPCGUtilsPointShorthandDouble(0.25, TEXT("OffsetAmount"));

	/**
	 * Which grid axes the offset moves along (default X, Y and Z). X | Y offsets sideways only: no layers are added or
	 * removed above or below, and the top and bottom faces stay where they are. No axis leaves every cell as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelAxes"))
	int32 Axes = static_cast<int32>(EPCGVoxelAxes::All);

	/** Cells (default): 1 is one cell width. Absolute: world units, the same distance on every axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetUnits Units = EPCGVoxelOffsetUnits::Cells;

	/**
	 * Multiplies Amount, for falloff or masking: 0 leaves a cell alone, negative flips it. Default constant 1 (no
	 * change). Attribute (a number, required in that mode; default 'AmountScale'): a point attribute or property gives
	 * each cell its own scale (every cell of a chunk shares its point's value); '@Data.AmountScale' gives one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDouble AmountScale = FPCGUtilsPointShorthandDouble(1.0, TEXT("AmountScale"));

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteWasSplit = true;

	/**
	 * Optional output, Boolean: true on each of the boxes a cell was carved into at an inside corner (Split Inner
	 * Corners); false on every other point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteWasSplit"))
	FName WasSplitAttribute = TEXT("WasSplit");

	/** The node stops with an error when the box of cells it works over (the set's bounds, plus the added layers) has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
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
