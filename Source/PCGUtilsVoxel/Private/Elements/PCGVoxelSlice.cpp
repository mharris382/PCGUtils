// Copyright Max Harris

#include "Elements/PCGVoxelSlice.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelSlice"

namespace PCGVoxelSlicePrivate
{
	/** Floor of A / B for B > 0, also for negative A. */
	int32 FloorDiv(int32 A, int32 B)
	{
		const int32 Q = A / B;
		return (A % B != 0 && A < 0) ? Q - 1 : Q;
	}

	/** One point's part in one slice: cells First..Last along the axis. */
	struct FPiece
	{
		int32 Point = INDEX_NONE;
		int32 First = 0;
		int32 Last = 0;
	};
}

#if WITH_EDITOR
FText UPCGVoxelSliceSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Slice");
}

FText UPCGVoxelSliceSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Cuts each voxel set into slabs across one grid axis, one set per slice, on the input's grid. Chunks crossing a cut "
		"are split there. Writes each slice's index and its 0-1 place in the stack to @Data, so Transform Grid can turn each "
		"slice a little more than the last (after Separate Islands, about each island's centre).");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelSliceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets. Each is cut into its own slices."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelSliceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One voxel set per slice that has cells, input by input, lowest slice first: the input's points "
			"in that slice (split chunks cut to it), with the input's metadata, @Data, tags and grid, plus the slice outputs in @Data."));
	return Pins;
}

FPCGElementPtr UPCGVoxelSliceSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelSliceElement>();
}

bool FPCGVoxelSliceElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelSliceElement::ExecuteInternal);
	using namespace PCGVoxelSlicePrivate;
	check(Context);

	const UPCGVoxelSliceSettings* Settings = Context->GetInputSettings<UPCGVoxelSliceSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Slice");

	if (Settings->Thickness < 1)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
			LOCTEXT("Thickness", "Thickness is {0}; it must be at least 1 cell."), FText::AsNumber(Settings->Thickness))), Context);
		return true;
	}

	// Every enabled output needs a name of its own, and none may overwrite the grid's own attributes.
	struct FOutputName { bool bOn; FName Name; const TCHAR* Setting; };
	const FOutputName Names[] = {
		{ Settings->bWriteSliceIndex, Settings->SliceIndexAttribute, TEXT("Slice Index Attribute") },
		{ Settings->bWriteSliceFraction, Settings->SliceFractionAttribute, TEXT("Slice Fraction Attribute") },
		{ Settings->bWriteSliceCount, Settings->SliceCountAttribute, TEXT("Slice Count Attribute") },
		{ Settings->bWriteSliceDistance, Settings->SliceDistanceAttribute, TEXT("Slice Distance Attribute") },
	};
	const FName GridTransformName = Settings->Grid.GridTransformAttribute;
	const FName VoxelSizeName = Settings->Grid.VoxelSize.IsConstant() ? NAME_None : Settings->Grid.VoxelSize.Attribute.GetAttributeName();
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
		if ((!GridTransformName.IsNone() && Names[A].Name == GridTransformName) || Names[A].Name == VoxelSizeName)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
				LOCTEXT("GridName", "{0} is '{1}', which is the grid's own {2} attribute; pick another name."),
				FText::FromString(Names[A].Setting), FText::FromName(Names[A].Name),
				Names[A].Name == GridTransformName ? LOCTEXT("GridTransformWord", "Grid Transform") : LOCTEXT("VoxelSizeWord", "Voxel Size"))), Context);
			return true;
		}
	}

	const int32 Axis = static_cast<int32>(Settings->Axis);
	const int32 Thickness = Settings->Thickness;
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}
		if (Grid.NumCells() == 0)
		{
			continue;
		}

		// Slice s covers cells Base + s * Thickness .. Base + (s + 1) * Thickness - 1 along the axis.
		const int32 Base = Settings->Start == EPCGVoxelSliceStart::SetBounds ? Grid.MinCell[Axis] : 0;
		auto SliceOf = [&](int32 Cell) { return FloorDiv(Cell - Base, Thickness); };
		const int32 LowSlice = SliceOf(Grid.MinCell[Axis]);
		const int32 HighSlice = SliceOf(Grid.MaxCell[Axis]);

		// Every point (an ignored duplicate too: it goes where its cell goes) in every slice it reaches.
		TMap<int32, TArray<FPiece>> Slices;
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			const int32 First = Grid.PointFirst[Point][Axis];
			const int32 Last = First + Grid.PointSpan[Point][Axis] - 1;
			for (int32 Slice = SliceOf(First); Slice <= SliceOf(Last); ++Slice)
			{
				const int32 SliceFirst = Base + Slice * Thickness;
				Slices.FindOrAdd(Slice).Add({ Point, FMath::Max(First, SliceFirst), FMath::Min(Last, SliceFirst + Thickness - 1) });
			}
		}
		TArray<int32> Order;
		Slices.GetKeys(Order);
		Order.Sort();

		// Whole-set occupancy, for Surrounded with cut faces hidden.
		auto InWholeSet = [&Grid](const FIntVector& Cell) { return Grid.IsOccupied(Cell); };
		const bool bHasSurrounded = !Settings->Grid.SurroundedAttribute.IsNone()
			&& PCGVoxelPointIO::HasPointAttribute(Points, Settings->Grid.SurroundedAttribute);

		const FTransform& G = Grid.GridToWorld;
		FVector AxisUnit = FVector::ZeroVector;
		AxisUnit[Axis] = 1.0;
		const FVector AxisWorld = G.GetRotation().RotateVector(AxisUnit);
		const double Size = Grid.VoxelSize[Axis];

		for (const int32 Slice : Order)
		{
			const TArray<FPiece>& Pieces = Slices[Slice];
			const int32 Num = Pieces.Num();

			UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
			Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
			Out->SetNumPoints(Num, /*bInitializeValues=*/false);
			Out->AllocateProperties(Points->GetAllocatedProperties() | EPCGPointNativeProperties::Transform
				| EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed);
			{
				TArray<int32> Read, Write;
				Read.SetNumUninitialized(Num);
				Write.SetNumUninitialized(Num);
				for (int32 I = 0; I < Num; ++I)
				{
					Read[I] = Pieces[I].Point;
					Write[I] = I;
				}
				Points->CopyPointsTo(Out, Read, Write);
			}

			// Cut the pieces of split chunks: centre, bounds and seed. Outer sides keep the chunk's surface offset; a cut
			// side gets the natural extent. The point's local axis nearest the grid axis takes the change.
			TArray<FPCGVoxelCellOut> Boxes;
			Boxes.SetNum(Num);
			bool bAnySplit = false;
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			TPCGValueRange<int32> Seeds = Out->GetSeedValueRange();
			for (int32 I = 0; I < Num; ++I)
			{
				const FPiece& Piece = Pieces[I];
				const FIntVector ChunkFirst = Grid.PointFirst[Piece.Point];
				const FIntVector ChunkSpan = Grid.PointSpan[Piece.Point];
				FPCGVoxelCellOut& Box = Boxes[I];
				Box.SourcePoint = Piece.Point;
				Box.First = ChunkFirst;
				Box.Span = ChunkSpan;
				Box.First[Axis] = Piece.First;
				Box.Span[Axis] = Piece.Last - Piece.First + 1;
				if (Box.Span[Axis] == ChunkSpan[Axis])
				{
					continue;
				}
				bAnySplit = true;

				FTransform& Transform = Transforms[I];
				const FVector LocalAxis = Transform.GetRotation().UnrotateVector(AxisWorld);
				const int32 K = FMath::Abs(LocalAxis.X) >= FMath::Abs(LocalAxis.Y)
					? (FMath::Abs(LocalAxis.X) >= FMath::Abs(LocalAxis.Z) ? 0 : 2)
					: (FMath::Abs(LocalAxis.Y) >= FMath::Abs(LocalAxis.Z) ? 1 : 2);
				const double Sign = LocalAxis[K] >= 0.0 ? 1.0 : -1.0;
				double Scale = FMath::Abs(Transform.GetScale3D()[K]);
				if (Scale < UE_SMALL_NUMBER)
				{
					Scale = 1.0;
				}

				// Along the grid axis, relative to the point's position (the chunk's middle).
				const double GridLow = Sign > 0.0 ? Mins[I][K] * Scale : -Maxs[I][K] * Scale;
				const double GridHigh = Sign > 0.0 ? Maxs[I][K] * Scale : -Mins[I][K] * Scale;
				const double ChunkHalf = ChunkSpan[Axis] * Size * 0.5;
				const double PieceLow = (Piece.First - ChunkFirst[Axis]) * Size - ChunkHalf;
				const double PieceHigh = (Piece.Last + 1 - ChunkFirst[Axis]) * Size - ChunkHalf;
				const double Low = Piece.First == ChunkFirst[Axis] ? GridLow : PieceLow;
				const double High = Piece.Last == ChunkFirst[Axis] + ChunkSpan[Axis] - 1 ? GridHigh : PieceHigh;
				const double Middle = (PieceLow + PieceHigh) * 0.5;

				Transform.SetLocation(Transform.GetLocation() + AxisWorld * Middle);
				FVector Min = Mins[I], Max = Maxs[I];
				if (Sign > 0.0)
				{
					Min[K] = (Low - Middle) / Scale;
					Max[K] = (High - Middle) / Scale;
				}
				else
				{
					Min[K] = -(High - Middle) / Scale;
					Max[K] = -(Low - Middle) / Scale;
				}
				Mins[I] = Min;
				Maxs[I] = Max;
				Seeds[I] = PCGHelpers::ComputeSeed(Box.First.X, Box.First.Y, Box.First.Z);
			}

			const bool bWriteSpan = bAnySplit && Grid.bChunked;
			if (bWriteSpan || bHasSurrounded)
			{
				PCGVoxelPointIO::MakeEntriesUnique(Out);
			}
			if (bWriteSpan)
			{
				TArray<FVector> Spans;
				Spans.SetNumUninitialized(Num);
				for (int32 I = 0; I < Num; ++I)
				{
					Spans[I] = FVector(Boxes[I].Span);
				}
				PCGVoxelPointIO::WritePointAttribute<FVector>(Out, Settings->Grid.SpanAttribute, FVector(1.0), Spans);
			}
			if (bHasSurrounded)
			{
				if (Settings->bCutFacesAreOpen)
				{
					const FPCGVoxelCellSet SliceCells(Boxes);
					PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Boxes,
						[&SliceCells](const FIntVector& Cell) { return SliceCells.Contains(Cell); });
				}
				else
				{
					PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Boxes, InWholeSet);
				}
			}

			if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Out))
			{
				if (Settings->bWriteSliceIndex)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->SliceIndexAttribute, Slice);
				}
				if (Settings->bWriteSliceFraction)
				{
					const double Fraction = HighSlice > LowSlice ? double(Slice - LowSlice) / double(HighSlice - LowSlice) : 0.0;
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->SliceFractionAttribute, Fraction);
				}
				if (Settings->bWriteSliceCount)
				{
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->SliceCountAttribute, HighSlice - LowSlice + 1);
				}
				if (Settings->bWriteSliceDistance)
				{
					const double MiddleCell = Base + Slice * Thickness + (Thickness - 1) * 0.5;
					PCGUtilsDataDomain::WriteValue(*Domain, Settings->SliceDistanceAttribute, MiddleCell * Size);
				}
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
			Output.Data = Out;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
