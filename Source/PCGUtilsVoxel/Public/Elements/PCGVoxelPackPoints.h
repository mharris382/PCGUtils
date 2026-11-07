// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGElement.h"

#include "PCGVoxelPackPoints.generated.h"

/** Where a point's settle amount (0 = stays where it was, 1 = fully in its cell) comes from. */
UENUM(BlueprintType)
enum class EPCGVoxelSettleSource : uint8
{
	/** The same amount for every point. */
	Constant,
	/** A random amount between Min and Max, from the point's seed and the node seed. */
	Random,
	/** A point attribute (0 to 1). */
	Attribute,
};

/** A settle amount: how far, from 0 to 1, a point moves (or turns) from its own pose to its packed pose. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelSettleAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable))
	EPCGVoxelSettleSource Source = EPCGVoxelSettleSource::Constant;

	/** Constant only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable, ClampMin="0", ClampMax="1",
		EditCondition="Source == EPCGVoxelSettleSource::Constant", EditConditionHides))
	double Value = 1.0;

	/** Random only. Must not be above Max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable, ClampMin="0", ClampMax="1",
		EditCondition="Source == EPCGVoxelSettleSource::Random", EditConditionHides))
	double Min = 0.0;

	/** Random only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable, ClampMin="0", ClampMax="1",
		EditCondition="Source == EPCGVoxelSettleSource::Random", EditConditionHides))
	double Max = 1.0;

	/** Attribute only. A numeric point attribute; every value must be from 0 to 1 (it is not clamped). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable,
		EditCondition="Source == EPCGVoxelSettleSource::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector Attribute;
};

/** What happens to a point that finds no free cell within Max Radius. */
UENUM(BlueprintType)
enum class EPCGVoxelUnpackedPoints : uint8
{
	/** It stays where it is, with Packed = false. */
	Keep,
	/** It is removed from the output. */
	Remove,
};

/** What happens to a box that collides with a box already placed, even when moved fully into its cell. */
UENUM(BlueprintType)
enum class EPCGVoxelUnresolvedOverlap : uint8
{
	/** It is removed from the output. */
	Remove,
	/** It stays where its settle amount put it, with Overlapping = true. */
	Keep,
	/** It goes back to where it started if that is free; otherwise it is removed. */
	Return,
};

/** How loose points that carry a span are packed. */
UENUM(BlueprintType)
enum class EPCGVoxelInChunks : uint8
{
	/** A point with a span claims a free box of that many cells, all at once. */
	WholePiece UMETA(DisplayName="Whole Piece"),
	/** Every point is one voxel; spans are ignored. */
	OneCell UMETA(DisplayName="One Cell"),
};

/** How the Unfilled Cells output writes grid chunks. */
UENUM(BlueprintType)
enum class EPCGVoxelChunkOutput : uint8
{
	/** One point per unfilled cell. */
	Expand,
	/** A chunk nothing claimed stays one point; a partly claimed chunk splits into its unfilled cells. */
	Keep,
};

/**
 * Packs loose points into the free cells of a voxel grid, fully or part way: each point is a voxel that fell out of
 * the grid. Points are handled in input order and each takes the nearest free cell within Max Radius; a cell takes one
 * point. A settle amount below 1 leaves a point between where it was and its cell, and with Prevent Overlaps no two
 * output boxes intersect. Out matches In point for point and keeps all metadata.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels pack fill settle cubes fall snap grid place cells blocks"))
class PCGUTILSVOXEL_API UPCGVoxelPackPointsSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelPackPointsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelPackPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	virtual bool UseSeed() const override { return true; }

	/** How far (world units) a point looks for a free cell centre. Must be greater than 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Packing", meta=(PCG_Overridable, ClampMin="0"))
	double MaxRadius = 1000.0;

	/** Keep (default) leaves a point with no free cell in range where it is; Remove drops it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Packing", meta=(PCG_Overridable))
	EPCGVoxelUnpackedPoints UnpackedPoints = EPCGVoxelUnpackedPoints::Keep;

	/** Whole Piece (default): a point with a span in In claims a box of that many cells. One Cell: spans are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Packing", meta=(PCG_Overridable))
	EPCGVoxelInChunks InChunks = EPCGVoxelInChunks::WholePiece;

	/** Write bounds the size of the cells a point fills, and scale 1. Bounds are never blended by the settle amount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Packing", meta=(PCG_Overridable))
	bool bSetBounds = true;

	/** How far each point moves from its position to its cell centre. Default: all the way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable))
	FPCGVoxelSettleAmount PositionSettle;

	/** How far each point turns from its rotation to the grid's. Default: all the way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settle", meta=(PCG_Overridable))
	FPCGVoxelSettleAmount RotationSettle;

	/**
	 * Keep output boxes (cell-sized, as placed) from intersecting. Fully settled boxes are placed first; a part-way box
	 * that collides is pushed further into its cell until it is free. Unresolved Overlap decides the rest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overlaps", meta=(PCG_Overridable))
	bool bPreventOverlaps = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overlaps", meta=(PCG_Overridable, EditCondition="bPreventOverlaps"))
	EPCGVoxelUnresolvedOverlap UnresolvedOverlap = EPCGVoxelUnresolvedOverlap::Remove;

	/** How the Unfilled Cells output writes grid chunks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	EPCGVoxelChunkOutput UnfilledChunks = EPCGVoxelChunkOutput::Expand;

	/** Bool written per point: whether it found a cell. None to skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName PackedAttribute = TEXT("Packed");

	/** Int32 written per point: the index of the Grid point whose cell it claimed (the first cell for a whole piece), or -1. None to skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName TargetIndexAttribute = TEXT("VoxelTargetIndex");

	/** Float written per point: the position settle amount actually used, after overlap pushing. None to skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName SettleAttribute = TEXT("Settle");

	/** Bool written per point when Unresolved Overlap is Keep: whether it still overlaps another box. None to skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName OverlappingAttribute = TEXT("Overlapping");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelPackPointsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

namespace PCGVoxelPackPoints
{
	inline const FName GridLabel = TEXT("Grid");
	inline const FName UnfilledLabel = TEXT("Unfilled Cells");

	/** Whether two oriented boxes intersect (separating axis test). Touching faces do not count. */
	PCGUTILSVOXEL_API bool BoxesOverlap(const FVector& CenterA, const FQuat& RotationA, const FVector& HalfA,
		const FVector& CenterB, const FQuat& RotationB, const FVector& HalfB);

	/**
	 * Of the rotations that map a box with half size Half onto itself (axis swaps between equal lengths, and flips),
	 * the one closest to Rotation. Both in grid space.
	 */
	PCGUTILSVOXEL_API FQuat ClosestBoxRotation(const FQuat& Rotation, const FVector& Half);
}
