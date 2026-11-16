// Copyright Max Harris

#include "Elements/PCGVoxelCompress.h"

#include "Data/PCGBasePointData.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelChunking.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelCompress"

namespace PCGVoxelCompressPrivate
{
	/** Palette entries: the plain node, and the same node preset to Shape Random. */
	constexpr int32 CompressPreset = 0;
	constexpr int32 CompressRandomPreset = 1;

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

		// What two cells must share (the shared compression rules, Voxel/PCGVoxelChunking.h).
		FPCGVoxelPointEquality Equal;
		Grid.ComputeSurfaceOffsets(Points, Equal.OffsetMin, Equal.OffsetMax);
		FName Missing;
		if (!Equal.Init(Points, Settings->MatchAttributes, Settings->ListedAttributes, Settings->Grid.SpanAttribute, Settings->Grid.SurroundedAttribute, Missing))
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
				LOCTEXT("ListedMissing", "Listed Attributes names '{0}', which is not a point attribute of this input."), FText::FromName(Missing))), Context);
			continue;
		}
		auto EqualRef = [&Equal](int32 A, int32 B) { return Equal(A, B); };

		TArray<PCGVoxelChunking::FBox> Boxes;
		if (Settings->Shape == EPCGVoxelChunkShape::Random)
		{
			PCGVoxelChunking::FRandomPlan Plan;
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
			PCGVoxelChunking::RandomBoxes(Grid, Plan, PCGHelpers::ComputeSeed(Context->GetSeed(), Index), EqualRef, Boxes);
		}
		else
		{
			PCGVoxelChunking::MergeCells(Grid.Cells, Grid.CellPoint, MaxSpan, Settings->Shape == EPCGVoxelChunkShape::Boxes, EqualRef, Boxes);
		}

		Boxes.Sort([](const PCGVoxelChunking::FBox& A, const PCGVoxelChunking::FBox& B) { return FPCGVoxelGrid::CellLess(A.First, B.First); });
		TArray<FPCGVoxelCellOut> Out;
		Out.Reserve(Boxes.Num());
		for (const PCGVoxelChunking::FBox& Box : Boxes)
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
