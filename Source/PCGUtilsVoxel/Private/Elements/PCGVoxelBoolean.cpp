// Copyright Max Harris

#include "Elements/PCGVoxelBoolean.h"

#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Spatial/PointHashGrid3.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelBoolean"

namespace PCGVoxelBooleanPrivate
{
	/** One Operand cell placed on In's grid, and where it came from. */
	struct FOperandCell
	{
		FIntVector Cell;
		int32 Operand;
		int32 Point;
	};

	FText VectorText(const FVector& V)
	{
		return FText::FromString(FString::Printf(TEXT("(%g, %g, %g)"), V.X, V.Y, V.Z));
	}
}

#if WITH_EDITOR
FText UPCGVoxelBooleanSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Boolean");
}

FText UPCGVoxelBooleanSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Union, intersect or subtract voxel sets cell by cell: cells are matched by grid index, so touching faces and rounding "
		"never matter. Out is each In set with Operand's cells added, kept or cut out; all Operand inputs count as one set and "
		"must share In's cell size and line up with its grid.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelBooleanSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets to change. One output per input, keeping its metadata, tags and @Data."))
		.SetRequiredPin();
	Pins.Emplace_GetRef(PCGVoxelBooleanPins::Operand, EPCGDataType::Point, true, true,
		LOCTEXT("OperandTooltip", "Voxel sets added to, kept from or cut out of every In set; together they count as one set of cells. "
			"Same cell size as In, lined up with its grid."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelBooleanSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each In set with its cells changed. Chunks stay whole where no cell of theirs changed; cut chunks become plain cells."));
	return Pins;
}

FPCGElementPtr UPCGVoxelBooleanSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelBooleanElement>();
}

bool FPCGVoxelBooleanElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelBooleanPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelBooleanElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelBooleanSettings* Settings = Context->GetInputSettings<UPCGVoxelBooleanSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Boolean");
	const FText OperandTitle = LOCTEXT("ErrorTitleOperand", "Voxel | Boolean: Operand");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	// ---- Operand sets, read once ----
	const TArray<FPCGTaggedData> OperandInputs = Context->InputData.GetInputsByPin(PCGVoxelBooleanPins::Operand);
	TArray<const UPCGBasePointData*> OperandPoints;
	TArray<FPCGVoxelGrid> OperandGrids;
	OperandGrids.SetNum(OperandInputs.Num());
	for (int32 Operand = 0; Operand < OperandInputs.Num(); ++Operand)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(OperandInputs[Operand].Data);
		if (!Points)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(OperandTitle, Operand, LOCTEXT("OperandNotPoints", "the input is not point data; it must be a voxel set.")), Context);
			return true;
		}
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, OperandTitle, Operand, OperandGrids[Operand]))
		{
			return true;
		}
		OperandPoints.Add(Points);
	}

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

		// ---- Place every Operand cell on this grid ----
		const double Tolerance = Settings->Grid.GridTolerance * Grid.VoxelSize.GetMin();
		const FQuat ToInGrid = Grid.GridToWorld.GetRotation().Inverse();
		TArray<FOperandCell> OperandCells;
		bool bAligned = true;
		for (int32 Operand = 0; Operand < OperandGrids.Num() && bAligned; ++Operand)
		{
			const FPCGVoxelGrid& Other = OperandGrids[Operand];
			const FVector SizeInGrid = (ToInGrid * Other.GridToWorld.GetRotation()).RotateVector(Other.VoxelSize).GetAbs();
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (FMath::Abs(SizeInGrid[Axis] - Grid.VoxelSize[Axis]) > Tolerance)
				{
					Fail(FText::Format(LOCTEXT("SizeMismatch", "Operand {0}'s Voxel Size, along this set's grid axes, is {1}; this set's is {2}. They must match."),
						Operand, VectorText(SizeInGrid), VectorText(Grid.VoxelSize)), InputIndex);
					bAligned = false;
					break;
				}
			}
			if (!bAligned)
			{
				break;
			}
			OperandCells.Reserve(OperandCells.Num() + Other.NumCells());
			for (int32 Index = 0; Index < Other.NumCells(); ++Index)
			{
				const FVector Local = Grid.GridToWorld.InverseTransformPositionNoScale(Other.CellCenterWorld(Other.Cells[Index])) / Grid.VoxelSize;
				const FIntVector Cell(FMath::RoundToInt32(Local.X), FMath::RoundToInt32(Local.Y), FMath::RoundToInt32(Local.Z));
				const double Off = ((Local - FVector(Cell)) * Grid.VoxelSize).Size();
				if (Off > Tolerance)
				{
					Fail(FText::Format(LOCTEXT("OffGrid", "Operand {0}'s cell {1} (point {2}) is {3} from the nearest cell centre of this set's grid, "
						"more than Grid Tolerance ({4}); the two grids do not line up."),
						Operand, FText::FromString(Other.Cells[Index].ToString()), Other.CellPoint[Index], FText::AsNumber(Off), FText::AsNumber(Tolerance)), InputIndex);
					bAligned = false;
					break;
				}
				OperandCells.Add({ Cell, Operand, Other.CellPoint[Index] });
			}
		}
		if (!bAligned)
		{
			continue;
		}

		// One entry per cell (the first Operand to fill it wins), ordered Z, Y, X.
		Algo::StableSort(OperandCells, [](const FOperandCell& A, const FOperandCell& B) { return FPCGVoxelGrid::CellLess(A.Cell, B.Cell); });
		{
			int32 Write = 0;
			for (int32 Read = 0; Read < OperandCells.Num(); ++Read)
			{
				if (Write == 0 || OperandCells[Read].Cell != OperandCells[Write - 1].Cell)
				{
					OperandCells[Write++] = OperandCells[Read];
				}
			}
			OperandCells.SetNum(Write);
		}
		FPCGVoxelGrid Lookup;
		Lookup.PointFirst.SetNumUninitialized(OperandCells.Num());
		Lookup.PointSpan.Init(FIntVector(1), OperandCells.Num());
		Lookup.PointUsed.Init(true, OperandCells.Num());
		for (int32 Index = 0; Index < OperandCells.Num(); ++Index)
		{
			Lookup.PointFirst[Index] = OperandCells[Index].Cell;
		}
		Lookup.BuildCells();

		// ---- The output cells ----
		TArray<FPCGVoxelCellOut> Outs;
		TArray<int32> AddedFrom; // Union: per added output point, its entry in OperandCells
		if (Settings->Operation == EPCGVoxelBooleanOperation::Union)
		{
			for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
			{
				if (Grid.PointUsed[Point])
				{
					Outs.Add({ Point, Grid.PointFirst[Point], Grid.PointSpan[Point] });
				}
			}
			for (int32 Index = 0; Index < OperandCells.Num(); ++Index)
			{
				if (!Grid.IsOccupied(OperandCells[Index].Cell))
				{
					AddedFrom.Add(Index);
				}
			}
			if (!AddedFrom.IsEmpty() && Grid.NumCells() == 0)
			{
				PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, LOCTEXT("EmptyUnion",
					"this set has no cells, so Union has no cell to take values from; it is output unchanged.")), Context);
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
				continue;
			}

			// Each added cell copies its nearest In cell (by distance between centres).
			const FVector Size = Grid.VoxelSize;
			const double HashCell = 4.0 * Size.GetMax();
			UE::Geometry::TPointHashGrid3<int32, double> Hash(HashCell, INDEX_NONE);
			for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
			{
				Hash.InsertPointUnsafe(Index, FVector(Grid.Cells[Index]) * Size);
			}
			TArray<int32> Nearest;
			Nearest.SetNumUninitialized(AddedFrom.Num());
			ParallelFor(AddedFrom.Num(), [&](int32 Added)
			{
				const FVector Query = FVector(OperandCells[AddedFrom[Added]].Cell) * Size;
				int32 Found = INDEX_NONE;
				for (double Radius = Size.GetMax() * 1.5; Radius <= HashCell * 8.0 && Found == INDEX_NONE; Radius *= 2.0)
				{
					Found = Hash.FindNearestInRadius(Query, Radius, [&](const int32& Index)
					{
						return FVector::DistSquared(FVector(Grid.Cells[Index]) * Size, Query);
					}).Key;
				}
				if (Found == INDEX_NONE)
				{
					// Far from every In cell: a plain scan is cheaper than growing the hash query further.
					double Best = TNumericLimits<double>::Max();
					for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
					{
						const double D = FVector::DistSquared(FVector(Grid.Cells[Index]) * Size, Query);
						if (D < Best)
						{
							Best = D;
							Found = Index;
						}
					}
				}
				Nearest[Added] = Found;
			});
			for (int32 Added = 0; Added < AddedFrom.Num(); ++Added)
			{
				Outs.Add({ Grid.CellPoint[Nearest[Added]], OperandCells[AddedFrom[Added]].Cell, FIntVector(1) });
			}
		}
		else
		{
			const bool bKeepShared = Settings->Operation == EPCGVoxelBooleanOperation::Intersect;
			TArray<FIntVector> Kept;
			for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
			{
				if (!Grid.PointUsed[Point])
				{
					continue;
				}
				const FIntVector First = Grid.PointFirst[Point];
				const FIntVector Span = Grid.PointSpan[Point];
				Kept.Reset();
				for (int32 Z = 0; Z < Span.Z; ++Z)
					for (int32 Y = 0; Y < Span.Y; ++Y)
						for (int32 X = 0; X < Span.X; ++X)
						{
							const FIntVector Cell = First + FIntVector(X, Y, Z);
							if (Lookup.IsOccupied(Cell) == bKeepShared)
							{
								Kept.Add(Cell);
							}
						}
				if (Kept.Num() == Span.X * Span.Y * Span.Z)
				{
					Outs.Add({ Point, First, Span });
				}
				else
				{
					for (const FIntVector& Cell : Kept)
					{
						Outs.Add({ Point, Cell, FIntVector(1) });
					}
				}
			}
		}

		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Outs, Settings->Grid.SpanAttribute, Grid.bChunked);

		// ---- Union: values of the added cells ----
		const int32 FirstAdded = Outs.Num() - AddedFrom.Num();
		if (!AddedFrom.IsEmpty() && Settings->AddedValues != EPCGVoxelBooleanAddedValues::Nearest)
		{
			FPCGMetadataDomain* OutDomain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
			if (Settings->AddedValues == EPCGVoxelBooleanAddedValues::None)
			{
				for (int32 Added = 0; Added < AddedFrom.Num(); ++Added)
				{
					Entries[FirstAdded + Added] = OutDomain->AddEntry();
				}
			}
			else
			{
				Out->AllocateProperties(EPCGPointNativeProperties::Color | EPCGPointNativeProperties::Density | EPCGPointNativeProperties::Steepness);
				TPCGValueRange<FVector4> Colors = Out->GetColorValueRange();
				TPCGValueRange<float> Densities = Out->GetDensityValueRange();
				TPCGValueRange<float> Steepnesses = Out->GetSteepnessValueRange();
				for (int32 Operand = 0; Operand < OperandPoints.Num(); ++Operand)
				{
					const UPCGBasePointData* From = OperandPoints[Operand];
					const TConstPCGValueRange<int64> FromEntries = From->GetConstMetadataEntryValueRange();
					const TConstPCGValueRange<FVector4> FromColors = From->GetConstColorValueRange();
					const TConstPCGValueRange<float> FromDensities = From->GetConstDensityValueRange();
					const TConstPCGValueRange<float> FromSteepnesses = From->GetConstSteepnessValueRange();

					// The Operand's attributes, created on the output when In lacks them; skipped when In has one by that name of another type.
					TArray<TPair<const FPCGMetadataAttributeBase*, FPCGMetadataAttributeBase*>> Pairs;
					if (const FPCGMetadataDomain* FromDomain = From->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements))
					{
						TArray<FName> Names;
						TArray<EPCGMetadataTypes> Types;
						FromDomain->GetAttributes(Names, Types);
						for (const FName Name : Names)
						{
							const FPCGMetadataAttributeBase* FromAttribute = FromDomain->GetConstAttribute(Name);
							if (!FromAttribute || Name == Settings->Grid.SpanAttribute || Name == Settings->Grid.SurroundedAttribute)
							{
								continue;
							}
							FPCGMetadataAttributeBase* ToAttribute = OutDomain->GetMutableAttribute(Name);
							if (!ToAttribute)
							{
								ToAttribute = OutDomain->CopyAttribute(FromAttribute, Name, /*bKeepParent=*/false, /*bCopyEntries=*/false, /*bCopyValues=*/false);
							}
							if (ToAttribute && ToAttribute->GetTypeId() == FromAttribute->GetTypeId())
							{
								Pairs.Emplace(FromAttribute, ToAttribute);
							}
						}
					}

					for (int32 Added = 0; Added < AddedFrom.Num(); ++Added)
					{
						const FOperandCell& Source = OperandCells[AddedFrom[Added]];
						if (Source.Operand != Operand)
						{
							continue;
						}
						const int32 P = FirstAdded + Added;
						for (const TPair<const FPCGMetadataAttributeBase*, FPCGMetadataAttributeBase*>& Pair : Pairs)
						{
							Pair.Value->SetValue(Entries[P], Pair.Key, FromEntries[Source.Point]);
						}
						Colors[P] = FromColors[Source.Point];
						Densities[P] = FromDensities[Source.Point];
						Steepnesses[P] = FromSteepnesses[Source.Point];
					}
				}
			}
		}

		// Last, so values copied into added cells above cannot overwrite it.
		{
			const FPCGVoxelCellSet Filled(Outs);
			PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Outs,
				[&Filled](const FIntVector& Cell) { return Filled.Contains(Cell); });
		}
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
