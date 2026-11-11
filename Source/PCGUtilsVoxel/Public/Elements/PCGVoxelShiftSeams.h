// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelShiftSeams.generated.h"

/** Where each seam's shift comes from. */
UENUM(BlueprintType)
enum class EPCGVoxelSeamShiftSource : uint8
{
	/** An independent random value per seam, from the node seed. */
	Random,
	/** 3D Perlin noise sampled at the seam's centre, so nearby seams shift alike. */
	Noise,
};

/** What limits how far one seam can move. */
UENUM(BlueprintType)
enum class EPCGVoxelSeamLimit : uint8
{
	/**
	 * The thinnest box on either side of the seam (its full thickness along the seam's axis), so the reach scales with
	 * the chunks: big boxes against big boxes move far, anything next to a single cell moves at most a cell.
	 */
	ThinnestEitherSide UMETA(DisplayName="Thinnest Box, Either Side"),
	/** Half the thinnest box on the side that shrinks (the first version of this node). */
	HalfShrinkingSide UMETA(DisplayName="Half the Thinnest Shrinking Box"),
};

/**
 * Slides the interior seams of a voxel set so its boxes become uneven while still filling exactly the same space. A
 * seam is a connected flat patch of faces between boxes; moving it grows the boxes on one side and shrinks those on
 * the other by the same amount, so nothing overlaps and no gap opens. Seams touching the outside are fixed, so the
 * outer shape never changes. Cells and spans are kept: only point bounds change. Use after Voxel | Compress Random,
 * whose short, staggered seams can each move on their own.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels seams shift jitter uneven irregular tiles bricks warp nonuniform bounds"))
class PCGUTILSVOXEL_API UPCGVoxelShiftSeamsSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelShiftSeamsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelShiftSeams"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	virtual bool UseSeed() const override { return true; }

	/** Shift seams that face along grid X (they move along X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bShiftX = true;

	/** Shift seams that face along grid Y (they move along Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bShiftY = true;

	/** Shift seams that face along grid Z (they move along Z). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bShiftZ = true;

	/**
	 * How far a seam may move, as a fraction of its Limit (0 to 1). With the default limit, 1 lets a seam move the full
	 * thickness of the thinnest box beside it (that box can shrink to nothing); 0.5 half of it.
	 * Attribute (a number from 0 to 1, required in that mode; default 'SeamStrength'): a point attribute gives each box
	 * its own strength and a seam uses the weakest box on either side (0 on a box pins every seam it touches), for
	 * masking where seams move; '@Data.SeamStrength' gives one strength per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDouble01 Strength = FPCGUtilsPointShorthandDouble01(0.5, TEXT("SeamStrength"));

	/**
	 * What one seam's reach is measured against, along its axis, from the boxes' current sizes. Thinnest Box, Either
	 * Side (default): the smaller of the thinnest box on each side, so compressed chunks move further than cells.
	 * Half the Thinnest Shrinking Box: the first version's limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelSeamLimit Limit = EPCGVoxelSeamLimit::ThinnestEitherSide;

	/** Random (default): every seam on its own. Noise: seams follow a smooth 3D noise field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelSeamShiftSource Source = EPCGVoxelSeamShiftSource::Random;

	/**
	 * Noise only. Size of the noise features, in world units. Must be greater than 0. @Data (a number, required in that
	 * mode; default '@Data.SeamNoiseScale'): one size per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Source == EPCGVoxelSeamShiftSource::Noise", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs NoiseScale = FPCGUtilsInputShorthandDoubleAbs(400.0);

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelShiftSeamsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
