// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelTransformGrid.generated.h"

/** The space Translation, Rotation and a Location pivot are given in. */
UENUM(BlueprintType)
enum class EPCGVoxelTransformSpace : uint8
{
	/** Along the voxel set's own grid axes, so "rotate about X" turns about the grid's X however the grid sits in the world. */
	Grid,
	/** World axes and world positions. */
	World,
};

/** Where a voxel set turns and scales about. */
UENUM(BlueprintType)
enum class EPCGVoxelTransformPivot : uint8
{
	/** A point in the set's own cell bounds, given as a fraction per grid axis (0 = min side, 0.5 = centre, 1 = max side). */
	Bounds,
	/** A position, in Space. */
	Location,
};

/**
 * Moves, turns and scales whole voxel sets. Each input is one set (one grid); its grid transform and Voxel Size are
 * updated and every point moves with them, so the cells, spans and per-cell values are unchanged and every Voxel node
 * still reads the result. Every value can be a constant or read from each set's @Data, so a graph that splits a grid
 * into parts and writes a rotation and pivot per part turns each part on its own (a twisting corridor of slices).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels transform rotate rotation turn twist move translate offset scale pivot grid"))
class PCGUTILSVOXEL_API UPCGVoxelTransformGridSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelTransformGridSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelTransformGrid"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** The space Translation, Rotation and a Location pivot are in. Scale is always along the grid's own axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelTransformSpace Space = EPCGVoxelTransformSpace::Grid;

	/** Moves the set, in Space. Optional; @Data mode reads a Vector (default @Data.Translation) from each set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector Translation;

	/** Turns the set about the pivot, in Space. Optional; @Data mode reads a Rotator (default @Data.Rotation) from each set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandRotator Rotation;

	/**
	 * Scales the set about the pivot along its grid axes: Voxel Size is multiplied by it, so cells stay cells. Every
	 * axis must be greater than 0. Anything but 1 writes the new size to the Voxel Size attribute, so Voxel Size must
	 * be read from @Data. Optional; @Data mode reads a Vector (default @Data.Scale) from each set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector Scale;

	/** What the set turns and scales about: a spot in its own cell bounds, or a position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivot", meta=(PCG_Overridable))
	EPCGVoxelTransformPivot Pivot = EPCGVoxelTransformPivot::Bounds;

	/**
	 * Bounds only. Where the pivot sits in the set's cell bounds, per grid axis: 0 = min side, 0.5 = centre, 1 = max
	 * side (values outside 0-1 put it outside the bounds). @Data mode reads a Vector (default @Data.PivotAlignment)
	 * from each set, so every set can turn about its own spot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivot", meta=(PCG_Overridable,
		EditCondition="Pivot == EPCGVoxelTransformPivot::Bounds", EditConditionHides))
	FPCGUtilsInputShorthandVector PivotAlignment;

	/**
	 * Location only. The pivot position, in Space (grid space is measured from the grid origin, a cell centre).
	 * @Data mode reads a Vector (default @Data.Pivot) from each set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivot", meta=(PCG_Overridable,
		EditCondition="Pivot == EPCGVoxelTransformPivot::Location", EditConditionHides))
	FPCGUtilsInputShorthandVector PivotLocation;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelTransformGridElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
