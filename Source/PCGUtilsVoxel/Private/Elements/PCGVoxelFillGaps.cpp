// Copyright Max Harris

#include "Elements/PCGVoxelFillGaps.h"

#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelMorphology.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelFillGaps"

#if WITH_EDITOR
FText UPCGVoxelFillGapsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Fill Gaps");
}

FText UPCGVoxelFillGapsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Fills a voxel set's thin gaps (slots, notches and holes narrower than 2 x Gap Radius + 1 cells, along Axes: what "
		"a closing fills) and, with Fill Cavities, its enclosed cavities. Out is the input with the cells added, each "
		"copying the nearest filled cell; Gaps has only the added cells.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelFillGapsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets whose gaps to fill. One output per input on each pin, keeping its metadata, tags and @Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelFillGapsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input with its gaps filled, still a valid voxel set: its own points as they were (chunks "
			"kept), plus one point per added cell copied from the nearest filled cell. Is Surrounded is rewritten."));
	Pins.Emplace(PCGVoxelFillGapsPins::Gaps, EPCGDataType::Point, true, true,
		LOCTEXT("GapsTooltip", "Each input's added cells only, as a voxel set of their own (same grid, metadata and tags)."));
	return Pins;
}

FPCGElementPtr UPCGVoxelFillGapsSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelFillGapsElement>();
}

bool FPCGVoxelFillGapsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelFillGapsElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelFillGapsSettings* Settings = Context->GetInputSettings<UPCGVoxelFillGapsSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Fill Gaps");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	// Every enabled output needs a name of its own.
	if (Settings->bWriteIsGap && Settings->IsGapAttribute.IsNone())
	{
		Fail(LOCTEXT("EmptyGapName", "Is Gap Attribute is empty while its output is on; give it a name or turn the output off."));
		return true;
	}
	if (Settings->bWriteIsCavity && Settings->IsCavityAttribute.IsNone())
	{
		Fail(LOCTEXT("EmptyCavityName", "Is Cavity Attribute is empty while its output is on; give it a name or turn the output off."));
		return true;
	}
	if (Settings->bWriteIsGap && Settings->bWriteIsCavity && Settings->IsGapAttribute == Settings->IsCavityAttribute)
	{
		Fail(FText::Format(LOCTEXT("SameName", "Is Gap Attribute and Is Cavity Attribute are both '{0}'; each enabled output needs its own attribute name."),
			FText::FromName(Settings->IsGapAttribute)));
		return true;
	}

	FIntVector Reach(0);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Reach[Axis] = (Settings->Axes & (1 << Axis)) != 0 ? FMath::Max(1, Settings->GapRadius) : 0;
	}
	// Room for the closing (dilate then erode, twice the reach) and one empty layer for the outside flood.
	const FIntVector Pad(2 * FMath::Max(1, Settings->GapRadius) + 1);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		if (!Points)
		{
			Fail(LOCTEXT("NotPoints", "the input is not point data; it must be a voxel set."), InputIndex);
			continue;
		}
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid))
		{
			continue;
		}
		if (Grid.NumCells() == 0 || (!Settings->bFillGaps && !Settings->bFillCavities))
		{
			Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]).Pin = PCGPinConstants::DefaultOutputLabel;
			continue;
		}

		const FIntVector Box = Grid.MaxCell - Grid.MinCell + FIntVector(1) + Pad * 2;
		const int64 BoxCells = static_cast<int64>(Box.X) * Box.Y * Box.Z;
		if (BoxCells > Settings->MaxCells || BoxCells >= MAX_int32)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to work over is {0} x {1} x {2} = {3} cells (the set's bounds plus "
				"2 x Gap Radius + 1 each side), over Max Cells ({4}, and under 2^31 at most). Raise Max Cells, lower Gap Radius, or split the set."),
				Box.X, Box.Y, Box.Z, FText::AsNumber(BoxCells), FText::AsNumber(Settings->MaxCells)), InputIndex);
			continue;
		}

		// ---- Which empty cells to fill ----
		FPCGVoxelDenseMask Solid;
		Solid.Init(Grid.MinCell, Grid.MaxCell, Pad);
		for (const FIntVector& Cell : Grid.Cells)
		{
			Solid.Bits[Solid.Index(Cell)] = 1;
		}
		FPCGVoxelDenseMask Closed;
		if (Settings->bFillGaps)
		{
			PCGVoxelMorphology::Close(Solid, Reach, Closed);
		}
		FPCGVoxelDenseMask Outside;
		PCGVoxelMorphology::FloodOutside(Solid, Outside);

		TArray<FIntVector> Added;
		TArray<bool> AddedCavity;
		FPCGVoxelDenseMask AddedMask;
		AddedMask.InitLike(Solid);
		for (int64 I = 0; I < Solid.Num(); ++I)
		{
			if (Solid.Bits[I])
			{
				continue;
			}
			const bool bCavity = !Outside.Bits[I];
			const bool bGap = Settings->bFillGaps && Closed.Bits[I];
			if (bGap || (bCavity && Settings->bFillCavities))
			{
				AddedMask.Bits[I] = 1;
				Added.Add(Solid.Cell(I));
				AddedCavity.Add(bCavity);
			}
		}

		// ---- Each added cell copies the nearest filled cell: breadth-first (face steps) through the added cells ----
		TArray<int32> Owner; // per dense cell: the In point it copies
		Owner.Init(INDEX_NONE, Solid.Num());
		TArray<int64> Queue;
		for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
		{
			const int64 I = Solid.Index(Grid.Cells[Index]);
			Owner[I] = Grid.CellPoint[Index];
			Queue.Add(I);
		}
		for (int64 Head = 0; Head < Queue.Num(); ++Head)
		{
			static const FIntVector Faces[6] = {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
			const FIntVector Cell = Solid.Cell(Queue[Head]);
			for (const FIntVector& Step : Faces)
			{
				const FIntVector Next = Cell + Step;
				if (!AddedMask.Get(Next))
				{
					continue;
				}
				const int64 N = Solid.Index(Next);
				if (Owner[N] == INDEX_NONE)
				{
					Owner[N] = Owner[Queue[Head]];
					Queue.Add(N);
				}
			}
		}
		TArray<FPCGVoxelCellOut> GapOuts;
		GapOuts.Reserve(Added.Num());
		for (const FIntVector& Cell : Added)
		{
			int32 Source = Owner[Solid.Index(Cell)];
			if (Source == INDEX_NONE)
			{
				// Not joined to a filled cell through added cells: the nearest by distance.
				double Best = TNumericLimits<double>::Max();
				for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
				{
					const double D = (FVector(Grid.Cells[Index] - Cell) * Grid.VoxelSize).SizeSquared();
					if (D < Best)
					{
						Best = D;
						Source = Grid.CellPoint[Index];
					}
				}
			}
			GapOuts.Add({ Source, Cell, FIntVector(1) });
		}

		// ---- Write ----
		TArray<FPCGVoxelCellOut> Outs;
		Outs.Reserve(Grid.NumPoints() + GapOuts.Num());
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			if (Grid.PointUsed[Point])
			{
				Outs.Add({ Point, Grid.PointFirst[Point], Grid.PointSpan[Point] });
			}
		}
		const int32 NumOwn = Outs.Num();
		Outs.Append(GapOuts);

		const FQuat GridRotation = Grid.GridToWorld.GetRotation();
		const FVector Half = Grid.VoxelSize * 0.5;
		// Added cells: whole-cell bounds, and fresh entries for Added Values None.
		auto FinishAdded = [&](UPCGBasePointData* Data, int32 FirstAdded)
		{
			TPCGValueRange<FTransform> Transforms = Data->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Data->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Data->GetBoundsMaxValueRange();
			TPCGValueRange<int64> Entries = Data->GetMetadataEntryValueRange();
			FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			for (int32 Index = FirstAdded; Index < Data->GetNumPoints(); ++Index)
			{
				const FQuat ToPoint = Transforms[Index].GetRotation().Inverse() * GridRotation;
				FBox Local(ForceInit);
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					Local += ToPoint.RotateVector(FVector((Corner & 1) ? Half.X : -Half.X, (Corner & 2) ? Half.Y : -Half.Y, (Corner & 4) ? Half.Z : -Half.Z));
				}
				FVector Scale = Transforms[Index].GetScale3D().GetAbs();
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Scale[Axis] < UE_SMALL_NUMBER)
					{
						Scale[Axis] = 1.0;
					}
				}
				Mins[Index] = Local.Min / Scale;
				Maxs[Index] = Local.Max / Scale;
				if (Settings->AddedValues == EPCGVoxelOffsetAddedValues::None && Domain)
				{
					Entries[Index] = Domain->AddEntry();
				}
			}
		};
		auto IsFilled = [&](const FIntVector& Cell) { return Solid.Get(Cell) || AddedMask.Get(Cell); };
		auto WriteFlags = [&](UPCGBasePointData* Data, int32 FirstAdded)
		{
			if (Settings->bWriteIsGap)
			{
				TArray<bool> Values;
				Values.Init(false, Data->GetNumPoints());
				for (int32 Index = FirstAdded; Index < Values.Num(); ++Index)
				{
					Values[Index] = true;
				}
				PCGVoxelPointIO::WritePointAttribute<bool>(Data, Settings->IsGapAttribute, false, Values);
			}
			if (Settings->bWriteIsCavity)
			{
				TArray<bool> Values;
				Values.Init(false, Data->GetNumPoints());
				for (int32 Index = FirstAdded; Index < Values.Num(); ++Index)
				{
					Values[Index] = AddedCavity[Index - FirstAdded];
				}
				PCGVoxelPointIO::WritePointAttribute<bool>(Data, Settings->IsCavityAttribute, false, Values);
			}
		};

		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Outs, Settings->Grid.SpanAttribute, Grid.bChunked);
		FinishAdded(Out, NumOwn);
		PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Outs, IsFilled);
		WriteFlags(Out, NumOwn);
		FPCGTaggedData& OutTagged = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		OutTagged.Data = Out;
		OutTagged.Pin = PCGPinConstants::DefaultOutputLabel;

		UPCGBasePointData* Gaps = PCGVoxelPointIO::WriteCells(Context, Points, Grid, GapOuts, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false);
		FinishAdded(Gaps, 0);
		PCGVoxelPointIO::RefreshSurrounded(Points, Gaps, Settings->Grid.SurroundedAttribute, GapOuts, IsFilled);
		WriteFlags(Gaps, 0);
		FPCGTaggedData& GapsTagged = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		GapsTagged.Data = Gaps;
		GapsTagged.Pin = PCGVoxelFillGapsPins::Gaps;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
