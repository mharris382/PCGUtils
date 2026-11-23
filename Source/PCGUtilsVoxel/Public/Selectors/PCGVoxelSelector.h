// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"
#include "Factories/PCGUtilsFactoryData.h"

#include "PCGVoxelSelector.generated.h"

class FPCGUtilsElementPoints;
struct FPCGContext;
struct FPCGVoxelGrid;

/*
 * Voxel selectors: which cells of a grid something may touch (Voxel | Pattern's Selector pin). The voxel domain's
 * own selector type, as DynMesh has DynMesh Selectors and GC has GC Selections. See Docs/PCGUtilsSelectors.md.
 *
 * The contract is per cell: a selector is prepared against one grid and then asked about single cells. A selector
 * must answer for ANY cell coordinate, filled or empty: an Add pattern asks about the empty cells it would fill.
 */

USTRUCT(meta=(PCG_DataTypeDisplayName="Voxel Selector"))
struct FPCGVoxelSelectorDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSVOXEL_API);
};

namespace PCGVoxelSelectorPins
{
	/** The output of every node that makes a voxel selector, and the selector input of Voxel | Pattern. */
	inline const FName SelectorLabel = TEXT("Selector");
}

/**
 * One selector prepared against one grid. Used from one thread at a time, so it may cache answers. Returns the
 * selector's own answer: Invert is applied by the consumer (PCGVoxelSelectors::FSet), once.
 */
class PCGUTILSVOXEL_API FPCGVoxelSelectorOperation
{
public:
	virtual ~FPCGVoxelSelectorOperation() = default;

	/** Whether Cell (any grid coordinate, filled or empty) is selected. */
	virtual bool Test(const FIntVector& Cell) = 0;
};

/** A voxel selector on a wire. Never changed once emitted. Priority does not change the result: several selectors on one pin select a cell when any of them does. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel")
class PCGUTILSVOXEL_API UPCGVoxelSelectorData : public UPCGUtilsFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGVoxelSelectorDataTypeInfo)

	/** Select every cell this selector does not. Applied by the consumer, not by the operation. */
	UPROPERTY()
	bool bInvert = false;

	/**
	 * Prepares the selector against Grid, which outlives the returned operation. Null (already logged on the graph)
	 * when it cannot be evaluated. Context may be null in tests.
	 */
	virtual TSharedPtr<FPCGVoxelSelectorOperation> CreateOperation(FPCGContext* Context, const FPCGVoxelGrid& Grid) const
		PURE_VIRTUAL(UPCGVoxelSelectorData::CreateOperation, return nullptr;);

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** How a voxel cell is shown to a point selector. */
UENUM(BlueprintType)
enum class EPCGVoxelCellPoints : uint8
{
	/** One point at the cell's centre whose bounds are the cell's box. */
	CenterWithBounds UMETA(DisplayName="Center With Bounds"),
	/** One point at the cell's centre with no bounds. */
	Center,
};

/** How voxel cells become points for a point selector (Selector | nodes with Domain set to Voxel). */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelPointConversion
{
	GENERATED_BODY()

	/**
	 * What stands for a cell. Center With Bounds (default): a point at the cell's centre with the cell's box as its
	 * bounds, so a selector can ask for any part or all of the cell. Center: the centre alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion")
	EPCGVoxelCellPoints CellPoints = EPCGVoxelCellPoints::CenterWithBounds;
};

namespace PCGVoxelSelectors
{
	/** The data types a voxel selector pin accepts. */
	PCGUTILSVOXEL_API const TSet<FPCGDataTypeBaseId>& GetSelectorTypes();

	/**
	 * The voxel domain's conversion: one element per entry of Cells (its element id is the index in Cells), each one
	 * point in the world. Cells may be filled or empty. Appends to OutPoints.
	 */
	PCGUTILSVOXEL_API void CellsToPoints(const FPCGVoxelPointConversion& Conversion, const FPCGVoxelGrid& Grid,
		TConstArrayView<FIntVector> Cells, FPCGUtilsElementPoints& OutPoints);

	/** The selectors of one pin, prepared against one grid. None: every cell is selected. Several: a cell is selected when any of them selects it. */
	class PCGUTILSVOXEL_API FSet
	{
	public:
		/** False when a selector could not be prepared; OutFailed is then that selector. */
		bool Prepare(FPCGContext* Context, TConstArrayView<TObjectPtr<const UPCGVoxelSelectorData>> Selectors,
			const FPCGVoxelGrid& Grid, const UPCGVoxelSelectorData** OutFailed = nullptr);

		bool IsEmpty() const { return Operations.IsEmpty(); }

		/** Each selector's answer with its Invert applied, any of them. True when there are none. */
		bool Test(const FIntVector& Cell) const;

	private:
		struct FEntry
		{
			TSharedPtr<FPCGVoxelSelectorOperation> Operation;
			bool bInvert = false;
		};
		TArray<FEntry> Operations;
	};
}
