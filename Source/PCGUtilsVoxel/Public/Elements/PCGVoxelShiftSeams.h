// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
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
	 * How far a seam may move, as a fraction of half the thinnest box it touches (measured along the seam's axis).
	 * At 1 a box can shrink to nothing; at 0.5 every box keeps at least half its size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="0", ClampMax="1", UIMin="0", UIMax="1"))
	float Strength = 0.5f;

	/** Random (default): every seam on its own. Noise: seams follow a smooth 3D noise field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelSeamShiftSource Source = EPCGVoxelSeamShiftSource::Random;

	/** Noise only. Size of the noise features, in world units. Must be greater than 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Source == EPCGVoxelSeamShiftSource::Noise", EditConditionHides))
	float NoiseScale = 400.0f;

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
