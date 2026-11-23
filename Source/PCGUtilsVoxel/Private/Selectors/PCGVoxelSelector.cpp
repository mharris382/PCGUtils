// Copyright Max Harris

#include "Selectors/PCGVoxelSelector.h"

#include "Points/PCGUtilsElementPoints.h"
#include "Serialization/ArchiveCrc32.h"
#include "Voxel/PCGVoxelGrid.h"

PCG_DEFINE_TYPE_INFO(FPCGVoxelSelectorDataTypeInfo, UPCGVoxelSelectorData)

void UPCGVoxelSelectorData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	bool bLocalInvert = bInvert;
	Ar << bLocalInvert;
}

const TSet<FPCGDataTypeBaseId>& PCGVoxelSelectors::GetSelectorTypes()
{
	static const TSet<FPCGDataTypeBaseId> Types = { FPCGVoxelSelectorDataTypeInfo::AsId() };
	return Types;
}

void PCGVoxelSelectors::CellsToPoints(const FPCGVoxelPointConversion& Conversion, const FPCGVoxelGrid& Grid,
	TConstArrayView<FIntVector> Cells, FPCGUtilsElementPoints& OutPoints)
{
	OutPoints.Reserve(OutPoints.NumElements() + Cells.Num(), OutPoints.NumPoints() + Cells.Num());
	const FQuat Rotation = Grid.GridToWorld.GetRotation();
	const FBox CellBox(-Grid.VoxelSize * 0.5, Grid.VoxelSize * 0.5);
	const bool bWithBounds = Conversion.CellPoints == EPCGVoxelCellPoints::CenterWithBounds;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		OutPoints.BeginElement(Index);
		const FVector Center = Grid.CellCenterWorld(Cells[Index]);
		if (bWithBounds)
		{
			OutPoints.AddPoint(FTransform(Rotation, Center), CellBox);
		}
		else
		{
			OutPoints.AddPoint(Center);
		}
	}
}

bool PCGVoxelSelectors::FSet::Prepare(FPCGContext* Context, TConstArrayView<TObjectPtr<const UPCGVoxelSelectorData>> Selectors,
	const FPCGVoxelGrid& Grid, const UPCGVoxelSelectorData** OutFailed)
{
	Operations.Reset();
	for (const UPCGVoxelSelectorData* Selector : Selectors)
	{
		if (!Selector)
		{
			continue;
		}
		TSharedPtr<FPCGVoxelSelectorOperation> Operation = Selector->CreateOperation(Context, Grid);
		if (!Operation)
		{
			if (OutFailed)
			{
				*OutFailed = Selector;
			}
			Operations.Reset();
			return false;
		}
		Operations.Add({ MoveTemp(Operation), Selector->bInvert });
	}
	return true;
}

bool PCGVoxelSelectors::FSet::Test(const FIntVector& Cell) const
{
	if (Operations.IsEmpty())
	{
		return true;
	}
	for (const FEntry& Entry : Operations)
	{
		if (Entry.Operation->Test(Cell) != Entry.bInvert)
		{
			return true;
		}
	}
	return false;
}
