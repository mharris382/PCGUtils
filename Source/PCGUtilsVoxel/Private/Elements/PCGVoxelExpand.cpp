// Copyright Max Harris

#include "Elements/PCGVoxelExpand.h"

#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelExpand"

#if WITH_EDITOR
FText UPCGVoxelExpandSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Expand");
}

FText UPCGVoxelExpandSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Turns each chunk of a voxel set back into one point per cell. Cells copy their chunk's attributes and properties and "
		"are seeded the way the Volume Sampler seeds a cell. A set without chunks passes through unchanged.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelExpandSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets, chunked or plain."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelExpandSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One plain voxel set per input: one point per cell."));
	return Pins;
}

FPCGElementPtr UPCGVoxelExpandSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelExpandElement>();
}

bool FPCGVoxelExpandElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelExpandElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelExpandSettings* Settings = Context->GetInputSettings<UPCGVoxelExpandSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Expand");

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;

		bool bAllUsed = true;
		for (const bool bUsed : Grid.PointUsed)
		{
			bAllUsed &= bUsed;
		}
		if (!Grid.bChunked && bAllUsed)
		{
			continue; // Output already holds the input.
		}

		TArray<FPCGVoxelCellOut> Cells;
		Cells.Reserve(Grid.NumCells());
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			if (!Grid.PointUsed[Point])
			{
				continue;
			}
			const FIntVector First = Grid.PointFirst[Point];
			const FIntVector Span = Grid.PointSpan[Point];
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						Cells.Add({ Point, First + FIntVector(X, Y, Z), FIntVector(1) });
					}
				}
			}
		}
		Output.Data = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Cells, Settings->Grid.SpanAttribute,
			/*bWriteSpan=*/Grid.bChunked && !Settings->bRemoveSpanAttribute);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
