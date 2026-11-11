// Copyright Max Harris

#include "Elements/PCGVoxelCompress.h"

#include "Data/PCGBasePointData.h"
#include "Helpers/PCGHelpers.h"
#include "Math/RandomStream.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelCompress"

namespace PCGVoxelCompressPrivate
{
	/** Palette entries: the plain node, and the same node preset to Shape Random. */
	constexpr int32 CompressPreset = 0;
	constexpr int32 CompressRandomPreset = 1;

	/** Rounds down to a multiple of Size, also for negative values. */
	int32 FloorAlign(int32 Value, int32 Size)
	{
		return (Value >= 0 ? Value / Size : -((-Value + Size - 1) / Size)) * Size;
	}

	FIntVector FloorAlign(const FIntVector& Cell, int32 Size)
	{
		return FIntVector(FloorAlign(Cell.X, Size), FloorAlign(Cell.Y, Size), FloorAlign(Cell.Z, Size));
	}

	/** Whether two points may share a chunk: equal surface offsets and equal compared values. */
	struct FPointEquality
	{
		TArray<const FPCGMetadataAttributeBase*> Attributes;
		bool bColor = false;
		bool bDensity = false;
		bool bSteepness = false;
		TConstPCGValueRange<int64> Entries;
		TConstPCGValueRange<FVector4> Colors;
		TConstPCGValueRange<float> Densities;
		TConstPCGValueRange<float> Steepnesses;
		TArray<FVector> OffsetMin;
		TArray<FVector> OffsetMax;

		bool operator()(int32 A, int32 B) const
		{
			if (A == B)
			{
				return true;
			}
			if (!OffsetMin[A].Equals(OffsetMin[B], 1e-6) || !OffsetMax[A].Equals(OffsetMax[B], 1e-6))
			{
				return false;
			}
			if ((bColor && Colors[A] != Colors[B]) || (bDensity && Densities[A] != Densities[B]) || (bSteepness && Steepnesses[A] != Steepnesses[B]))
			{
				return false;
			}
			for (const FPCGMetadataAttributeBase* Attribute : Attributes)
			{
				if (!Attribute->AreValuesEqualForEntryKeys(Entries[A], Entries[B]))
				{
					return false;
				}
			}
			return true;
		}
	};

	struct FChunkBox
	{
		FIntVector First;
		FIntVector Span;
		int32 Rep = INDEX_NONE;
	};

	/** Joins neighbouring equal boxes along Axis where their other two extents match. */
	void JoinAlong(TArray<FChunkBox>& Boxes, int32 Axis, int32 MaxSpan, const FPointEquality& Equal)
	{
		const int32 A1 = (Axis + 1) % 3;
		const int32 A2 = (Axis + 2) % 3;
		Boxes.Sort([&](const FChunkBox& L, const FChunkBox& R)
		{
			if (L.First[A2] != R.First[A2]) return L.First[A2] < R.First[A2];
			if (L.First[A1] != R.First[A1]) return L.First[A1] < R.First[A1];
			if (L.Span[A2] != R.Span[A2]) return L.Span[A2] < R.Span[A2];
			if (L.Span[A1] != R.Span[A1]) return L.Span[A1] < R.Span[A1];
			return L.First[Axis] < R.First[Axis];
		});
		TArray<FChunkBox> Joined;
		Joined.Reserve(Boxes.Num());
		for (const FChunkBox& Box : Boxes)
		{
			if (!Joined.IsEmpty())
			{
				FChunkBox& Last = Joined.Last();
				const bool bSameRow = Last.First[A1] == Box.First[A1] && Last.First[A2] == Box.First[A2]
					&& Last.Span[A1] == Box.Span[A1] && Last.Span[A2] == Box.Span[A2];
				if (bSameRow && Last.First[Axis] + Last.Span[Axis] == Box.First[Axis]
					&& Last.Span[Axis] + Box.Span[Axis] <= MaxSpan && Equal(Last.Rep, Box.Rep))
				{
					Last.Span[Axis] += Box.Span[Axis];
					continue;
				}
			}
			Joined.Add(Box);
		}
		Boxes = MoveTemp(Joined);
	}

	/** Shape Random's per-start-cell inputs: the size mode, each point's span and chance. */
	struct FRandomPlan
	{
		EPCGVoxelRandomSize Mode = EPCGVoxelRandomSize::UpTo;
		FIntVector SingleSpan = FIntVector(3);
		TArray<FIntVector> PointSpan; // empty: SingleSpan for every point
		TPCGUtilsPointValues<double> Chance;

		const FIntVector& SpanOf(int32 Point) const { return PointSpan.IsEmpty() ? SingleSpan : PointSpan[Point]; }
	};

	/**
	 * Random tiling: cells in Z, Y, X order; each cell not yet taken starts a box, sized by the plan from that start
	 * cell's span and chance, and grown only over cells that are occupied, free and equal to the start cell. Every cell
	 * before the start in that order is already taken, so the start is always the box's lowest corner.
	 */
	void RandomBoxes(const FPCGVoxelGrid& Grid, const FRandomPlan& Plan, int32 Seed, const FPointEquality& Equal, TArray<FChunkBox>& Boxes)
	{
		FRandomStream Stream(Seed);
		TArray<bool> Taken;
		Taken.Init(false, Grid.NumCells());

		auto SlabFree = [&](const FIntVector& First, const FIntVector& Span, int32 Axis, int32 Rep)
		{
			// The layer of cells just past the box along Axis.
			FIntVector Lo = First;
			Lo[Axis] += Span[Axis];
			FIntVector Size = Span;
			Size[Axis] = 1;
			for (int32 Z = 0; Z < Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 X = 0; X < Size.X; ++X)
					{
						const int32 Cell = Grid.FindCellIndex(Lo + FIntVector(X, Y, Z));
						if (Cell == INDEX_NONE || Taken[Cell] || !Equal(Rep, Grid.CellPoint[Cell]))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		for (int32 Start = 0; Start < Grid.NumCells(); ++Start)
		{
			if (Taken[Start])
			{
				continue;
			}
			const FIntVector First = Grid.Cells[Start];
			const int32 Rep = Grid.CellPoint[Start];
			const FIntVector MaxSpan = Plan.SpanOf(Rep);

			FIntVector Span(1);
			if (Plan.Mode == EPCGVoxelRandomSize::UpTo)
			{
				// The draws are the same as before the size modes existed, so older graphs keep their layout.
				const FIntVector Target(Stream.RandRange(1, MaxSpan.X), Stream.RandRange(1, MaxSpan.Y), Stream.RandRange(1, MaxSpan.Z));
				int32 Order[3] = { 0, 1, 2 };
				for (int32 I = 2; I > 0; --I)
				{
					Swap(Order[I], Order[Stream.RandRange(0, I)]);
				}
				for (const int32 Axis : Order)
				{
					while (Span[Axis] < Target[Axis] && SlabFree(First, Span, Axis, Rep))
					{
						++Span[Axis];
					}
				}
			}
			else if (Plan.Mode == EPCGVoxelRandomSize::Exact)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					while (Span[Axis] < MaxSpan[Axis] && SlabFree(First, Span, Axis, Rep))
					{
						++Span[Axis];
					}
				}
				if (Span != MaxSpan)
				{
					Span = FIntVector(1); // the whole box does not fit: no compression here
				}
			}
			else
			{
				// A cube grows a layer on X, Y and Z together, or not at all.
				const int32 Side = Stream.RandRange(1, FMath::Min3(MaxSpan.X, MaxSpan.Y, MaxSpan.Z));
				int32 Size = 1;
				while (Size < Side)
				{
					FIntVector Trial(Size);
					bool bFits = true;
					for (int32 Axis = 0; Axis < 3 && bFits; ++Axis)
					{
						bFits = SlabFree(First, Trial, Axis, Rep);
						++Trial[Axis];
					}
					if (!bFits)
					{
						break;
					}
					++Size;
				}
				Span = FIntVector(Size);
			}

			// The chance is rolled only when there is something to compress, and only when below 1 (so a chance of 1
			// draws nothing and keeps older layouts).
			if (Span != FIntVector(1))
			{
				const double Chance = Plan.Chance.Get(Rep);
				if (Chance < 1.0 && !(Stream.FRand() < Chance))
				{
					Span = FIntVector(1);
				}
			}

			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						Taken[Grid.FindCellIndex(First + FIntVector(X, Y, Z))] = true;
					}
				}
			}
			Boxes.Add({ First, Span, Rep });
		}
	}

	/** A Random Span value as whole cell counts, each at least 1. */
	bool ToSpanVector(const FVector& Value, FIntVector& OutSpan)
	{
		return PCGVoxel::ToSpan(Value.X, OutSpan.X) && PCGVoxel::ToSpan(Value.Y, OutSpan.Y) && PCGVoxel::ToSpan(Value.Z, OutSpan.Z);
	}
}

UPCGVoxelCompressSettings::UPCGVoxelCompressSettings()
{
	MaxSpan.Attribute.Update(TEXT("@Data.CompressMaxSpan"));
}

void UPCGVoxelCompressSettings::PostLoad()
{
	Super::PostLoad();
	// Random Max Span (an FIntVector) became Random Span (a point shorthand). Zero means the old default was in use.
	if (RandomMaxSpan_DEPRECATED != FIntVector::ZeroValue)
	{
		RandomSpan.Input = EPCGUtilsPointInputValueType::Constant;
		RandomSpan.Constant = FVector(RandomMaxSpan_DEPRECATED);
		RandomMaxSpan_DEPRECATED = FIntVector::ZeroValue;
	}
}

#if WITH_EDITOR
FText UPCGVoxelCompressSettings::GetDefaultNodeTitle() const
{
	return Shape == EPCGVoxelChunkShape::Random ? LOCTEXT("RandomTitle", "Voxel | Compress Random") : LOCTEXT("Title", "Voxel | Compress");
}

FText UPCGVoxelCompressSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Merges equal neighbouring cells of a voxel set into chunks (one point per box of cells, span in @VoxelSpan). "
		"Cells merge only when equal in every value (or in Listed Attributes) and in surface offset, so other Voxel nodes "
		"give the same result. With Listed, cells keep only the first cell's values for attributes not listed. "
		"Shape Random makes boxes of random size instead, for looks.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGVoxelCompressSettings::GetPreconfiguredInfo() const
{
	using namespace PCGVoxelCompressPrivate;
	return {
		{ CompressPreset, LOCTEXT("CompressPreset", "Voxel | Compress"),
			LOCTEXT("CompressPresetTooltip", "Merges equal neighbouring cells into as few chunks as it can.") },
		{ CompressRandomPreset, LOCTEXT("CompressRandomPreset", "Voxel | Compress Random"),
			LOCTEXT("CompressRandomPresetTooltip", "Merges equal neighbouring cells into boxes of random size: a staggered tiling, for looks.") },
	};
}

void UPCGVoxelCompressSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Shape = PreconfiguredInfo.PreconfiguredIndex == PCGVoxelCompressPrivate::CompressRandomPreset
		? EPCGVoxelChunkShape::Random
		: EPCGVoxelChunkShape::Cubes;
}
#endif

TArray<FPCGPinProperties> UPCGVoxelCompressSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets, plain or already chunked."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelCompressSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One chunked voxel set per input, ordered by first cell, with the Span Attribute on every point."));
	return Pins;
}

FPCGElementPtr UPCGVoxelCompressSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelCompressElement>();
}

bool FPCGVoxelCompressElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelCompressPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelCompressElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelCompressSettings* Settings = Context->GetInputSettings<UPCGVoxelCompressSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Compress");

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		int32 MaxSpan = Settings->MaxSpan.Constant;
		if (Settings->Shape != EPCGVoxelChunkShape::Random)
		{
			FText ReadError;
			if (!Settings->MaxSpan.TryReadDataValue(Points, MaxSpan, ReadError, LOCTEXT("MaxSpanLabel", "Max Span")))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, ReadError), Context);
				continue;
			}
			if (MaxSpan < 1)
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
					LOCTEXT("MaxSpanBad", "Max Span is {0}; it must be at least 1."), MaxSpan)), Context);
				continue;
			}
		}

		// What two cells must share.
		FPointEquality Equal;
		Equal.Entries = Points->GetConstMetadataEntryValueRange();
		Equal.Colors = Points->GetConstColorValueRange();
		Equal.Densities = Points->GetConstDensityValueRange();
		Equal.Steepnesses = Points->GetConstSteepnessValueRange();
		Grid.ComputeSurfaceOffsets(Points, Equal.OffsetMin, Equal.OffsetMax);
		const FPCGMetadataDomain* Domain = Points->ConstMetadata() ? Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		bool bListFailed = false;
		if (Settings->MatchAttributes == EPCGVoxelMatchAttributes::All)
		{
			Equal.bColor = Equal.bDensity = Equal.bSteepness = true;
			if (Domain)
			{
				TArray<FName> Names;
				TArray<EPCGMetadataTypes> Types;
				Domain->GetAttributes(Names, Types);
				for (const FName Name : Names)
				{
					// The span and 'is surrounded' describe the box, not the cell: they are rewritten for every chunk.
					if (Name != Settings->Grid.SpanAttribute && Name != Settings->Grid.SurroundedAttribute)
					{
						Equal.Attributes.Add(Domain->GetConstAttribute(Name));
					}
				}
			}
		}
		else
		{
			for (const FName Name : Settings->ListedAttributes)
			{
				if (Name == TEXT("$Color")) { Equal.bColor = true; continue; }
				if (Name == TEXT("$Density")) { Equal.bDensity = true; continue; }
				if (Name == TEXT("$Steepness")) { Equal.bSteepness = true; continue; }
				const FPCGMetadataAttributeBase* Attribute = Domain ? Domain->GetConstAttribute(Name) : nullptr;
				if (!Attribute)
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
						LOCTEXT("ListedMissing", "Listed Attributes names '{0}', which is not a point attribute of this input."), FText::FromName(Name))), Context);
					bListFailed = true;
					break;
				}
				Equal.Attributes.Add(Attribute);
			}
		}
		if (bListFailed)
		{
			continue;
		}

		TArray<FChunkBox> Boxes;
		if (Settings->Shape == EPCGVoxelChunkShape::Random)
		{
			FRandomPlan Plan;
			Plan.Mode = Settings->RandomSize;
			FText ReadError;
			TPCGUtilsPointValues<FVector> Spans;
			if (!Settings->RandomSpan.TryRead(Points, Spans, ReadError, LOCTEXT("SpanLabel", "Random Span"))
				|| !Settings->RandomChance.TryRead(Points, Plan.Chance, ReadError, LOCTEXT("ChanceLabel", "Random Chance")))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, ReadError), Context);
				continue;
			}
			bool bBadSpan = false;
			if (Spans.IsPerPoint())
			{
				Plan.PointSpan.SetNumUninitialized(Spans.PerPoint.Num());
				for (int32 Point = 0; Point < Spans.PerPoint.Num() && !bBadSpan; ++Point)
				{
					if (Grid.PointUsed[Point] && !ToSpanVector(Spans.PerPoint[Point], Plan.PointSpan[Point]))
					{
						PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(LOCTEXT("PointSpanBad",
							"Random Span: '{0}' on point {1} is {2}; every component must be a whole number, at least 1."),
							Settings->RandomSpan.Attribute.GetDisplayText(), Point, FText::FromString(Spans.PerPoint[Point].ToString()))), Context);
						bBadSpan = true;
					}
				}
			}
			else if (!ToSpanVector(Spans.Single, Plan.SingleSpan))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(LOCTEXT("SpanBad",
					"Random Span is {0}; every component must be a whole number, at least 1."), FText::FromString(Spans.Single.ToString()))), Context);
				bBadSpan = true;
			}
			if (bBadSpan)
			{
				continue;
			}
			RandomBoxes(Grid, Plan, PCGHelpers::ComputeSeed(Context->GetSeed(), Index), Equal, Boxes);
		}
		else
		{
			// Aligned octree: eight equal full cubes of size S at a multiple of 2S become one cube of size 2S.
			TMap<FIntVector, int32> Level;
			Level.Reserve(Grid.NumCells());
			for (const FIntVector& Cell : Grid.Cells)
			{
				Level.Add(Cell, Grid.FindPoint(Cell));
			}
			int32 Size = 1;
			while (Size * 2 <= MaxSpan && !Level.IsEmpty())
			{
				TMap<FIntVector, int32> ChildCount;
				for (const TPair<FIntVector, int32>& Node : Level)
				{
					++ChildCount.FindOrAdd(FloorAlign(Node.Key, Size * 2));
				}
				TMap<FIntVector, int32> Next;
				TSet<FIntVector> Merged;
				for (const TPair<FIntVector, int32>& Parent : ChildCount)
				{
					if (Parent.Value != 8)
					{
						continue;
					}
					const int32 Rep = Level.FindChecked(Parent.Key);
					bool bEqual = true;
					for (int32 Child = 1; Child < 8 && bEqual; ++Child)
					{
						const FIntVector Offset((Child & 1) ? Size : 0, (Child & 2) ? Size : 0, (Child & 4) ? Size : 0);
						bEqual = Equal(Rep, Level.FindChecked(Parent.Key + Offset));
					}
					if (bEqual)
					{
						Next.Add(Parent.Key, Rep);
						Merged.Add(Parent.Key);
					}
				}
				for (const TPair<FIntVector, int32>& Node : Level)
				{
					if (!Merged.Contains(FloorAlign(Node.Key, Size * 2)))
					{
						Boxes.Add({ Node.Key, FIntVector(Size), Node.Value });
					}
				}
				Level = MoveTemp(Next);
				Size *= 2;
			}
			for (const TPair<FIntVector, int32>& Node : Level)
			{
				Boxes.Add({ Node.Key, FIntVector(Size), Node.Value });
			}

			if (Settings->Shape == EPCGVoxelChunkShape::Boxes)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					JoinAlong(Boxes, Axis, MaxSpan, Equal);
				}
			}
		}

		Boxes.Sort([](const FChunkBox& A, const FChunkBox& B) { return FPCGVoxelGrid::CellLess(A.First, B.First); });
		TArray<FPCGVoxelCellOut> Out;
		Out.Reserve(Boxes.Num());
		for (const FChunkBox& Box : Boxes)
		{
			Out.Add({ Box.Rep, Box.First, Box.Span });
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		UPCGBasePointData* Written = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Out, Settings->Grid.SpanAttribute, /*bWriteSpan=*/true);
		PCGVoxelPointIO::RefreshSurrounded(Points, Written, Settings->Grid.SurroundedAttribute, Out,
			[&Grid](const FIntVector& Cell) { return Grid.IsOccupied(Cell); });
		Output.Data = Written;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
