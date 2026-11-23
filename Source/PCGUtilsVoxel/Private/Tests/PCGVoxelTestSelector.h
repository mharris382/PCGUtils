// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Selectors/PCGVoxelSelector.h"

#include "PCGVoxelTestSelector.generated.h"

/**
 * Test only: a voxel selector that answers with a function over the cell coordinate. Lets the voxel tests check
 * how Voxel | Apply Patterns uses selectors (scope, invert, several on a pin, empty cells) without any real
 * selector; the In Bounds versions of those checks are in PCGUtilsSelectors. Never made by a node.
 */
UCLASS(Hidden, NotBlueprintType)
class UPCGVoxelTestCellSelectorData : public UPCGVoxelSelectorData
{
	GENERATED_BODY()

public:
	TFunction<bool(const FIntVector& Cell)> Predicate;

	virtual TSharedPtr<FPCGVoxelSelectorOperation> CreateOperation(FPCGContext* Context, const FPCGVoxelGrid& Grid) const override
	{
		class FOperation final : public FPCGVoxelSelectorOperation
		{
		public:
			explicit FOperation(TFunction<bool(const FIntVector&)> InPredicate) : Predicate(MoveTemp(InPredicate)) {}
			virtual bool Test(const FIntVector& Cell) override { return Predicate && Predicate(Cell); }
			TFunction<bool(const FIntVector&)> Predicate;
		};
		return MakeShared<FOperation>(Predicate);
	}
};
