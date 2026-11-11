// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"

#include "PCGVoxelGrid.generated.h"

class UPCGBasePointData;
struct FPCGContext;

/** Where a voxel set's grid sits when its data has no grid transform attribute. */
UENUM(BlueprintType)
enum class EPCGVoxelMissingGridTransform : uint8
{
	/** The grid origin is world 0, unrotated. This is how the Volume Sampler samples. */
	World,
	/** The grid follows the PCG target actor's location and rotation (its scale is ignored). */
	TargetActor UMETA(DisplayName="Target Actor"),
};

/** What to do with a point whose centre is further from the nearest cell centre than Grid Tolerance. */
UENUM(BlueprintType)
enum class EPCGVoxelOffGridPoints : uint8
{
	/** The input fails with an error naming the point. */
	Error,
	/** The point is treated as the nearest cell. */
	Snap,
};

/** What to do when two points cover the same cell. */
UENUM(BlueprintType)
enum class EPCGVoxelDuplicateCells : uint8
{
	/** The input fails with an error naming both points and the cell. */
	Error,
	/** The earlier point keeps the cell; the later point is dropped whole. */
	KeepFirst UMETA(DisplayName="Keep First"),
	/** The later point takes the cell; the earlier point is dropped whole. */
	KeepLast UMETA(DisplayName="Keep Last"),
};

/**
 * How a node reads a voxel set: the attribute names of the voxel contract and what to do with points that break
 * it. Every Voxel | node that reads a voxel set has one of these.
 */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelGridReadSettings
{
	GENERATED_BODY()

	FPCGVoxelGridReadSettings();

	/**
	 * Cell size along the grid's X, Y and Z. Every component must be greater than 0. Read from @Data.VoxelSize
	 * (a Vector) by default; switch to Constant to set it on the node instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector VoxelSize;

	/**
	 * Optional @Data attribute (Transform) placing the grid in the world: where its origin cell centre is and how it
	 * is rotated. Its scale must be 1; size lives in Voxel Size. When the data has no such attribute, Missing Grid
	 * Transform decides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	FName GridTransformAttribute = TEXT("VoxelGridTransform");

	/** Where the grid sits when the data has no Grid Transform Attribute. World matches the Volume Sampler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	EPCGVoxelMissingGridTransform MissingGridTransform = EPCGVoxelMissingGridTransform::World;

	/**
	 * Optional point attribute (Vector of whole numbers, each at least 1): how many cells a point covers along grid
	 * X, Y and Z. Its presence makes the set chunked; a point then sits at the centre of the box of cells it covers.
	 * Without it every point is one cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	FName SpanAttribute = TEXT("VoxelSpan");

	/**
	 * Optional point attribute (Boolean): true when a point's cell or chunk shows no face (every cell sharing a face
	 * with its box is filled), the usual filter for spawning only visible voxels. Voxel | Cell Properties writes it.
	 * When the input carries it, every node that changes cells (merges, splits, adds or removes them) rewrites it on
	 * its output, and Compress ignores it when deciding which cells are equal. None: never touched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	FName SurroundedAttribute = TEXT("IsSurrounded");

	/** How far a point's centre may be from where its cell (or chunk) centre should be, as a fraction of the smallest Voxel Size axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable, ClampMin="0", ClampMax="0.5"))
	double GridTolerance = 0.01;

	/** Error (default) fails the input on a point further than Grid Tolerance from the grid; Snap moves it to the nearest cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	EPCGVoxelOffGridPoints OffGridPoints = EPCGVoxelOffGridPoints::Error;

	/** Error (default) fails the input when two points cover one cell; Keep First / Keep Last drop one of them whole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	EPCGVoxelDuplicateCells DuplicateCells = EPCGVoxelDuplicateCells::Error;
};

/**
 * A voxel set read from point data and checked against the contract: the grid frame, which cells each point covers
 * and which point fills each cell. Chunks are expanded here, so code that asks about cells never sees them.
 */
struct PCGUTILSVOXEL_API FPCGVoxelGrid
{
	FVector VoxelSize = FVector(100.0);

	/** Grid space to world: location and rotation only. A cell's grid-space centre is Index * VoxelSize. */
	FTransform GridToWorld = FTransform::Identity;

	/** The data had the span attribute. */
	bool bChunked = false;

	/** Per point: the first (lowest) cell it covers, and how many cells it covers along each axis. */
	TArray<FIntVector> PointFirst;
	TArray<FIntVector> PointSpan;

	/** Per point: false when Duplicate Cells dropped it. A dropped point covers no cell. */
	TArray<bool> PointUsed;

	/** Every occupied cell, ordered by Z, then Y, then X. */
	TArray<FIntVector> Cells;

	/** The point filling each entry of Cells. */
	TArray<int32> CellPoint;

	/** Lowest and highest occupied cell on each axis. Meaningless when Cells is empty. */
	FIntVector MinCell = FIntVector::ZeroValue;
	FIntVector MaxCell = FIntVector::ZeroValue;

	/**
	 * Reads Points against the contract. Logs one error naming the exact failed invariant (prefixed with NodeTitle and
	 * InputIndex) and returns false when the input cannot be used.
	 */
	static bool Build(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGVoxelGridReadSettings& Settings,
		const FText& NodeTitle, int32 InputIndex, FPCGVoxelGrid& OutGrid);

	/**
	 * Reads only the grid frame (Voxel Size and grid transform) of Points. Same errors as Build for those two. Used
	 * by nodes that place things on a grid without reading its cells.
	 */
	static bool ReadFrame(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGVoxelGridReadSettings& Settings,
		const FText& NodeTitle, int32 InputIndex, FVector& OutVoxelSize, FTransform& OutGridToWorld);

	/**
	 * Fills the cell lookup from PointFirst/PointSpan/PointUsed set by hand (tests, nodes that make voxel sets).
	 * Returns false with the two clashing points in OutClash when two used points cover one cell.
	 */
	bool BuildCells(TPair<int32, int32>* OutClash = nullptr);

	int32 NumPoints() const { return PointFirst.Num(); }
	int32 NumCells() const { return Cells.Num(); }

	/** Where Cell is in Cells, or INDEX_NONE when it is empty. */
	int32 FindCellIndex(const FIntVector& Cell) const
	{
		if (bDense)
		{
			const FIntVector Local = Cell - DenseOrigin;
			if (static_cast<uint32>(Local.X) >= static_cast<uint32>(DenseDims.X)
				|| static_cast<uint32>(Local.Y) >= static_cast<uint32>(DenseDims.Y)
				|| static_cast<uint32>(Local.Z) >= static_cast<uint32>(DenseDims.Z))
			{
				return INDEX_NONE;
			}
			return DenseCell[Local.X + static_cast<int64>(DenseDims.X) * (Local.Y + static_cast<int64>(DenseDims.Y) * Local.Z)];
		}
		const int32* Found = SparseCell.Find(Cell);
		return Found ? *Found : INDEX_NONE;
	}

	/** The point filling Cell, or INDEX_NONE. */
	int32 FindPoint(const FIntVector& Cell) const
	{
		const int32 Index = FindCellIndex(Cell);
		return Index == INDEX_NONE ? INDEX_NONE : CellPoint[Index];
	}
	bool IsOccupied(const FIntVector& Cell) const { return FindCellIndex(Cell) != INDEX_NONE; }

	/** True when cells are looked up in a dense array over the bounding box rather than a hash map. */
	bool IsDense() const { return bDense; }

	FVector CellCenterGrid(const FIntVector& Cell) const { return FVector(Cell) * VoxelSize; }
	FVector CellCenterWorld(const FIntVector& Cell) const { return GridToWorld.TransformPositionNoScale(CellCenterGrid(Cell)); }

	/** Grid-space centre of the box of cells starting at First and covering Span. */
	FVector BoxCenterGrid(const FIntVector& First, const FIntVector& Span) const
	{
		return (FVector(First) + (FVector(Span) - FVector(1.0)) * 0.5) * VoxelSize;
	}

	/** The natural half size of a box covering Span cells. */
	FVector BoxHalfSize(const FIntVector& Span) const { return FVector(Span) * VoxelSize * 0.5; }

	/** Z, then Y, then X: the order Cells is in. */
	static bool CellLess(const FIntVector& A, const FIntVector& B)
	{
		return A.Z != B.Z ? A.Z < B.Z : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
	}

	/**
	 * Per point, how far its bounds reach past the natural extent of the cells it covers, on each side, measured
	 * outward (positive grows the surface). From point bounds times the point's scale; its rotation is ignored.
	 */
	void ComputeSurfaceOffsets(const UPCGBasePointData* Points, TArray<FVector>& OutMinSide, TArray<FVector>& OutMaxSide) const;

private:
	/**
	 * Cell lookup. A dense array over the occupied bounding box when that box is compact enough (a few times the
	 * cell count), else a hash map. While building it maps cells to points; once built, to indices in Cells.
	 */
	bool bDense = false;
	bool bStorageReady = false;
	FIntVector DenseOrigin = FIntVector::ZeroValue;
	FIntVector DenseDims = FIntVector::ZeroValue;
	TArray<int32> DenseCell;
	TMap<FIntVector, int32> SparseCell;

	void InitStorage(const FIntVector& Min, const FIntVector& Max, int64 NumCovered);
	int32* FindSlot(const FIntVector& Cell);
	void SetSlot(const FIntVector& Cell, int32 Value);
	void ClearSlot(const FIntVector& Cell);

	/** Turns the cell -> point storage into Cells, CellPoint and a cell -> index lookup. */
	void FinalizeCells();
};

namespace PCGVoxel
{
	/** "<NodeTitle>: input <Index>: <Message>", or without the input part when InputIndex is INDEX_NONE. */
	PCGUTILSVOXEL_API FText FormatError(const FText& NodeTitle, int32 InputIndex, const FText& Message);

	/** Rounds a span value; false when it is below 1 or not a whole number. */
	PCGUTILSVOXEL_API bool ToSpan(double Value, int32& OutSpan);
}
