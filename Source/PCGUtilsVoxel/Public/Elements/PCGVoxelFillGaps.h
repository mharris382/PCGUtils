// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelOffsetSurface.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelFillGaps.generated.h"

namespace PCGVoxelFillGapsPins
{
	inline const FName Gaps = TEXT("Gaps");
}

/**
 * Fills a voxel set's thin gaps and, optionally, its enclosed cavities. A gap is an empty cell a closing by Gap Radius
 * fills (Offset Surface by +R, then -R): a slot, notch or hole narrower than 2 x Gap Radius + 1 cells, measured along
 * Axes. A cavity is an empty cell no path through empty cells leads out of.
 *
 * Out: In, mutated and still a valid voxel set, with the filled cells added (chunks kept as they are; metadata, tags and
 * @Data kept). Each added cell copies the nearest filled cell (Added Values) with whole-cell bounds. Gaps: only the
 * added cells, as a voxel set of their own. Is Surrounded is rewritten on both.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels fill gaps gap slot notch hole cavity enclosed hollow closing close morphology patch seal"))
class PCGUTILSVOXEL_API UPCGVoxelFillGapsSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelFillGaps"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Fill thin gaps: empty cells a closing by Gap Radius fills (slots, notches and holes narrower than 2 x Gap Radius + 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bFillGaps = true;

	/** Gaps narrower than 2 x this + 1 cells are filled (1: one-cell slots, 2: up to three). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bFillGaps", ClampMin=1, ClampMax=16, UIMin=1, UIMax=8))
	int32 GapRadius = 1;

	/** The grid axes gaps are measured along (default X, Y and Z). X | Y fills slots seen in plan only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bFillGaps", Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelAxes"))
	int32 Axes = static_cast<int32>(EPCGVoxelAxes::All);

	/** Also fill enclosed cavities of any size: empty cells no face path through empty cells leads out of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bFillCavities = false;

	/** Where an added cell gets its values: Nearest copies the nearest filled cell; None uses attribute defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelOffsetAddedValues AddedValues = EPCGVoxelOffsetAddedValues::Nearest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsGap = true;

	/** Optional output, Boolean, on both pins: true on each cell this node added, false on the input's own cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteIsGap"))
	FName IsGapAttribute = TEXT("IsGap");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsCavity = false;

	/**
	 * Optional output, Boolean, on both pins: true on each added cell inside an enclosed cavity (also when Fill Gaps
	 * filled it because the cavity is small); false on every other point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteIsCavity"))
	FName IsCavityAttribute = TEXT("IsCavity");

	/** The node stops with an error when the box of cells it works over (the set's bounds padded by 2 x Gap Radius + 1) has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelFillGapsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
