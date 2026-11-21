// Copyright Max Harris

#include "Elements/PCGVoxelSeparateIslands.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelIslands.h"

#define LOCTEXT_NAMESPACE "PCGVoxelSeparateIslands"

UPCGVoxelSeparateIslandsSettings::UPCGVoxelSeparateIslandsSettings()
	: AnchorAlignment(FVector(0.5))
{
	// Update parses the @Data. prefix into the domain; the constructor's default name would keep it in the name.
	AnchorAlignment.Attribute.Update(TEXT("@Data.AnchorAlignment"));
}

#if WITH_EDITOR
FText UPCGVoxelSeparateIslandsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Separate Islands");
}

FText UPCGVoxelSeparateIslandsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Splits each voxel set into its connected parts: one set per island, with the island's points unchanged and the "
		"input's metadata, @Data and tags. Re-anchor Grid moves each island's grid origin to its own centre cell, so "
		"Transform Grid (Pivot = Location, Grid space, 0) turns each island, or any part later cut from it, about that centre.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelSeparateIslandsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets. Each is split into its own islands."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelSeparateIslandsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One voxel set per island, input by input, islands in cell order: the island's points with the "
			"input's metadata, @Data and tags, plus the island outputs and (re-anchored) grid transform in @Data."));
	return Pins;
}

FPCGElementPtr UPCGVoxelSeparateIslandsSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelSeparateIslandsElement>();
}

bool FPCGVoxelSeparateIslandsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelSeparateIslandsElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelSeparateIslandsSettings* Settings = Context->GetInputSettings<UPCGVoxelSeparateIslandsSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Separate Islands");

	// Every enabled output needs a name of its own, and none may overwrite the grid's own attributes.
	struct FOutputName { bool bOn; FName Name; const TCHAR* Setting; };
	const FOutputName Names[] = {
		{ Settings->bWriteIslandIndex, Settings->IslandIndexAttribute, TEXT("Island Index Attribute") },
		{ Settings->bWriteIslandCount, Settings->IslandCountAttribute, TEXT("Island Count Attribute") },
		{ Settings->bWriteCellCount, Settings->CellCountAttribute, TEXT("Cell Count Attribute") },
		{ Settings->bWriteBoundsCenter, Settings->BoundsCenterAttribute, TEXT("Bounds Center Attribute") },
	};
	const FName GridTransformName = Settings->Grid.GridTransformAttribute;
	const FName VoxelSizeName = Settings->Grid.VoxelSize.IsConstant() ? NAME_None : Settings->Grid.VoxelSize.Attribute.GetAttributeName();
	if (Settings->bReanchorGrid && GridTransformName.IsNone())
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE,
			LOCTEXT("NoGridTransformName", "Re-anchor Grid is on but Grid > Grid Transform Attribute is empty; name it or turn Re-anchor Grid off.")), Context);
		return true;
	}
	for (int32 A = 0; A < UE_ARRAY_COUNT(Names); ++A)
	{
		if (!Names[A].bOn)
		{
			continue;
		}
		if (Names[A].Name.IsNone())
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
				LOCTEXT("EmptyName", "{0} is empty while its output is on; give it a name or turn the output off."),
				FText::FromString(Names[A].Setting))), Context);
			return true;
		}
		for (int32 B = A + 1; B < UE_ARRAY_COUNT(Names); ++B)
		{
			if (Names[B].bOn && Names[B].Name == Names[A].Name)
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
					LOCTEXT("SameName", "{0} and {1} are both '{2}'; each output needs its own attribute."),
					FText::FromString(Names[A].Setting), FText::FromString(Names[B].Setting), FText::FromName(Names[A].Name))), Context);
				return true;
			}
		}
		if (Names[A].Name == GridTransformName || Names[A].Name == VoxelSizeName)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
				LOCTEXT("GridName", "{0} is '{1}', which is the grid's own {2} attribute; pick another name."),
				FText::FromString(Names[A].Setting), FText::FromName(Names[A].Name),
				Names[A].Name == GridTransformName ? LOCTEXT("GridTransformWord", "Grid Transform") : LOCTEXT("VoxelSizeWord", "Voxel Size"))), Context);
			return true;
		}
	}

	const int32 MinCells = FMath::Max(1, Settings->MinCells);
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		FVector Alignment(0.5);
		if (Settings->bReanchorGrid)
		{
			FText Error;
			if (!Settings->AnchorAlignment.TryReadDataValue(Points, Alignment, Error, LOCTEXT("AlignmentLabel", "Anchor Alignment"), Index))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Error), Context);
				continue;
			}
		}

		FPCGVoxelIslands Islands;
		PCGVoxel::FindIslands(Grid, Settings->Connectivity, Islands);

		// Kept islands, numbered without gaps, and each one's points in point order.
		TArray<int32> OutIsland;
		OutIsland.Init(INDEX_NONE, Islands.Num());
		TArray<int32> Kept;
		for (int32 Island = 0; Island < Islands.Num(); ++Island)
		{
			if (Islands.CellCount[Island] >= MinCells)
			{
				OutIsland[Island] = Kept.Add(Island);
			}
		}
		TArray<TArray<int32>> IslandPoints;
		IslandPoints.SetNum(Kept.Num());
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			const int32 Island = Islands.PointIsland[Point];
			if (Island != INDEX_NONE && OutIsland[Island] != INDEX_NONE)
			{
				IslandPoints[OutIsland[Island]].Add(Point);
			}
		}

		const FTransform& G = Grid.GridToWorld;
		for (int32 Out = 0; Out < Kept.Num(); ++Out)
		{
			const int32 Island = Kept[Out];
			const TArray<int32>& Read = IslandPoints[Out];

			UPCGBasePointData* Data = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
			Data->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
			Data->SetNumPoints(Read.Num(), /*bInitializeValues=*/false);
			Data->AllocateProperties(Points->GetAllocatedProperties());
			{
				TArray<int32> Write;
				Write.SetNumUninitialized(Read.Num());
				for (int32 I = 0; I < Read.Num(); ++I)
				{
					Write[I] = I;
				}
				Points->CopyPointsTo(Data, Read, Write);
			}

			// Island bounds in cell units: from the min side of the lowest cell to the max side of the highest.
			const FVector Lo = FVector(Islands.MinCell[Island]) - FVector(0.5);
			const FVector Hi = FVector(Islands.MaxCell[Island]) + FVector(0.5);

			if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Data))
			{
				if (Settings->bReanchorGrid)
				{
					// The cell nearest the aligned spot (ties toward the max side). Points stay put; only the origin moves.
					const FVector Spot = Lo + Alignment * (Hi - Lo);
					const FIntVector Anchor(FMath::FloorToInt(Spot.X + 0.5), FMath::FloorToInt(Spot.Y + 0.5), FMath::FloorToInt(Spot.Z + 0.5));
					const FTransform NewGrid(G.GetRotation(), G.TransformPositionNoScale(FVector(Anchor) * Grid.VoxelSize));
					PCGUtilsDataDomain::WriteValue(*Domain, GridTransformName, NewGrid);
				}
				if (Settings->bWriteIslandIndex)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->IslandIndexAttribute, Out);
				}
				if (Settings->bWriteIslandCount)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->IslandCountAttribute, Kept.Num());
				}
				if (Settings->bWriteCellCount)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->CellCountAttribute, Islands.CellCount[Island]);
				}
				if (Settings->bWriteBoundsCenter)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->BoundsCenterAttribute,
						G.TransformPositionNoScale((Lo + Hi) * 0.5 * Grid.VoxelSize));
				}
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
			Output.Data = Data;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
