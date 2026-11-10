// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelBoolean.generated.h"

namespace PCGVoxelBooleanPins
{
	const FName Operand = TEXT("Operand");
}

/** What Voxel | Boolean does with the In cells and the Operand cells. */
UENUM(BlueprintType)
enum class EPCGVoxelBooleanOperation : uint8
{
	/** In's cells plus every Operand cell In does not have. */
	Union,
	/** Only In's cells that Operand also has. */
	Intersect,
	/** In's cells that Operand does not have: Operand is cut out of In. */
	Subtract,
};

/** Where a cell that Union adds gets its values from. */
UENUM(BlueprintType)
enum class EPCGVoxelBooleanAddedValues : uint8
{
	/**
	 * The Operand cell's attributes, color, density and steepness; any In attribute the Operand lacks (or has with
	 * another type) comes from the nearest In cell. Operand attributes In lacks are added (In's cells get their default).
	 */
	OperandThenNearest UMETA(DisplayName="Operand, Then Nearest"),
	/** Everything from the nearest In cell, as if In had grown there. */
	Nearest,
	/** Attribute defaults; properties from the nearest In cell. */
	None,
};

/**
 * Union, intersect or subtract voxel sets cell by cell. Cells are matched by their index on In's grid, not by
 * overlapping bounds, so a cell is either in or out and nothing is lost to touching faces or rounding. Operand sets
 * must use In's cell size and line up with its grid (any whole-cell offset is fine); Voxel | Rasterize with In on its
 * Grid pin makes such a set from a mesh or path.
 *
 * Out: In, mutated: its points, metadata, tags and @Data, with cells removed (Intersect, Subtract) or added (Union).
 * All Operand inputs together count as one set of cells.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels boolean union intersect intersection subtract difference cut carve merge combine csg"))
class PCGUTILSVOXEL_API UPCGVoxelBooleanSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelBoolean"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Union adds Operand's cells, Intersect keeps only shared cells, Subtract cuts Operand's cells out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelBooleanOperation Operation = EPCGVoxelBooleanOperation::Subtract;

	/** Union: where an added cell's values come from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="Operation == EPCGVoxelBooleanOperation::Union", EditConditionHides))
	EPCGVoxelBooleanAddedValues AddedValues = EPCGVoxelBooleanAddedValues::OperandThenNearest;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelBooleanElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
