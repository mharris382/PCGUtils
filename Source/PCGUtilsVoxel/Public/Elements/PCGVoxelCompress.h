// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
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
 * Voxel node gives the same result on the compressed set. Voxel | Expand turns it back into cells.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels compress chunk chunks merge octree greedy minecraft span"))
class PCGUTILSVOXEL_API UPCGVoxelCompressSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelCompress"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Cubes (default) makes N x N x N chunks only; Boxes also joins equal chunks into uneven boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelChunkShape Shape = EPCGVoxelChunkShape::Cubes;

	/** Largest chunk, in cells, along any axis. Cubes are powers of two, so the biggest cube is the largest power of two not above this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int32 MaxSpan = 16;

	/** All (default) merges only cells equal in every value. Listed compares only Listed Attributes and compresses more, but loses the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelMatchAttributes MatchAttributes = EPCGVoxelMatchAttributes::All;

	/** Listed only. Point attributes compared; $Color, $Density and $Steepness are allowed. Every listed attribute must exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(
		EditCondition="MatchAttributes == EPCGVoxelMatchAttributes::Listed", EditConditionHides))
	TArray<FName> ListedAttributes;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelCompressElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
