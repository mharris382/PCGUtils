// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelCompress.generated.h"

/** What shapes Voxel | Compress merges cells into. */
UENUM(BlueprintType)
enum class EPCGVoxelChunkShape : uint8
{
	/** N x N x N chunks on an aligned octree: power-of-two cubes at multiples of their size. */
	Cubes,
	/** Cubes first, then neighbouring equal chunks joined along X, Y, then Z into boxes. Usually fewer points. */
	Boxes,
	/**
	 * Boxes of random size, for looks rather than fewer points: a staggered tiling like a wall of uneven slabs. Cells
	 * are visited in order (Z, then Y, then X); each cell not yet taken starts a box sized by Random Size, Random Span
	 * and Random Chance, read from that start cell. Uses the node seed.
	 */
	Random,
};

/** Shape Random: what size each box may be. */
UENUM(BlueprintType)
enum class EPCGVoxelRandomSize : uint8
{
	/** Each box draws a size from 1 to Random Span per axis and grows as far as free, equal cells allow. */
	UpTo UMETA(DisplayName="Up To"),
	/** Each box is exactly Random Span, or its start cell stays a single cell when that box does not fit. */
	Exact,
	/**
	 * Each box is a cube: it draws a side from 1 to the smallest axis of Random Span and becomes the largest cube up to
	 * that side that fits.
	 */
	Cubes UMETA(DisplayName="Cubes Up To"),
};

/** Which values two cells must share to be merged. */
UENUM(BlueprintType)
enum class EPCGVoxelMatchAttributes : uint8
{
	/** Every point attribute plus $Color, $Density and $Steepness. Nothing is lost. */
	All,
	/** Only the attributes in Listed Attributes. Cells keep only the first cell's values for everything else. */
	Listed,
};

/**
 * Merges equal neighbouring cells of a voxel set into chunks: one point covering a box of cells, with its span in the
 * Span Attribute. Cells merge only when they are equal (Match Attributes) and have the same surface offset, so every
 * Voxel node gives the same result on the compressed set. Voxel | Expand turns it back into cells. Shape Random (the
 * Voxel | Compress Random entry) picks random box sizes instead, as a stylistic choice.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels compress chunk chunks merge octree greedy minecraft span random tiles slabs bricks stagger"))
class PCGUTILSVOXEL_API UPCGVoxelCompressSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelCompress"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif
	virtual bool UseSeed() const override { return Shape == EPCGVoxelChunkShape::Random; }

	/**
	 * Cubes (default) makes N x N x N chunks only; Boxes also joins equal chunks into uneven boxes. Random makes boxes
	 * of random size, for looks (Random Size, Random Span, Random Chance).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelChunkShape Shape = EPCGVoxelChunkShape::Cubes;

	/**
	 * Largest chunk, in cells, along any axis; at least 1. Cubes are powers of two, so the biggest cube is the largest
	 * power of two not above this. @Data (an integer, required in that mode; default '@Data.CompressMaxSpan'): one
	 * value per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Shape != EPCGVoxelChunkShape::Random", EditConditionHides))
	FPCGUtilsInputShorthandInteger32Abs MaxSpan = FPCGUtilsInputShorthandInteger32Abs(16);

	/** Random only. Up To (default): any size up to Random Span. Exact: exactly Random Span or nothing. Cubes Up To: cubes only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Shape == EPCGVoxelChunkShape::Random", EditConditionHides))
	EPCGVoxelRandomSize RandomSize = EPCGVoxelRandomSize::UpTo;

	/**
	 * Random only. The box size, in cells, along grid X, Y and Z: the largest (Up To), the only one (Exact), or the
	 * largest cube side as its smallest axis (Cubes Up To). Every component must be a whole number, at least 1. Set an
	 * axis to 1 to keep boxes one cell thick along it.
	 * Attribute (a Vector, required in that mode; default 'CompressSpan'): a point attribute gives each start cell its
	 * own size (first come, first served: cells already taken by an earlier box are gone); '@Data.CompressSpan' one per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Shape == EPCGVoxelChunkShape::Random", EditConditionHides))
	FPCGUtilsPointShorthandVector RandomSpan = FPCGUtilsPointShorthandVector(FVector(3.0), TEXT("CompressSpan"));

	/**
	 * Random only. The chance (0 to 1) that a start cell becomes a box when a box bigger than one cell fits there;
	 * otherwise it stays a single cell. 1 (default) always compresses; 0.3 leaves about 70% of the boxes as loose cells.
	 * Attribute (a number from 0 to 1, required in that mode; default 'CompressChance'): read from the start cell, so
	 * an attribute steers where boxes form; '@Data.CompressChance' gives one chance per set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable,
		EditCondition="Shape == EPCGVoxelChunkShape::Random", EditConditionHides))
	FPCGUtilsPointShorthandDouble01 RandomChance = FPCGUtilsPointShorthandDouble01(1.0, TEXT("CompressChance"));

	/** All (default) merges only cells equal in every value. Listed compares only Listed Attributes and compresses more, but loses the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelMatchAttributes MatchAttributes = EPCGVoxelMatchAttributes::All;

	/** Listed only. Point attributes compared; $Color, $Density and $Steepness are allowed. Every listed attribute must exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(
		EditCondition="MatchAttributes == EPCGVoxelMatchAttributes::Listed", EditConditionHides))
	TArray<FName> ListedAttributes;

	UPCGVoxelCompressSettings();

	virtual void PostLoad() override;

protected:
	/** Older graphs: the largest random box, now Random Span. Zero means it was not saved (the old default). */
	UPROPERTY()
	FIntVector RandomMaxSpan_DEPRECATED = FIntVector::ZeroValue;

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelCompressElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
