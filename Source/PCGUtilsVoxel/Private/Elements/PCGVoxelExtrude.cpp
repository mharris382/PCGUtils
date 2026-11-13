// Copyright Max Harris

#include "Elements/PCGVoxelExtrude.h"

#include "Algo/Sort.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelExtrude"

namespace PCGVoxelExtrudePrivate
{
	/** Whole cells for a step length: whole numbers as they are, anything more rounded up. */
	int32 WholeSteps(double Length)
	{
		return FMath::Min(FMath::CeilToInt32(Length - 1e-6), 1000000);
	}

	/**
	 * Calls Visit(Offset, Step) for every cell a cell's extrude reaches (not the cell itself): straight runs per axis
	 * (Separate Axes) or the whole box. Step is the distance in cells along the run, or the largest axis distance in
	 * the box.
	 */
	template <typename FVisit>
	void ForEachReached(const FIntVector& Steps, bool bSeparateAxes, FVisit&& Visit)
	{
		if (bSeparateAxes)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 Count = FMath::Abs(Steps[Axis]);
				const int32 Sign = Steps[Axis] > 0 ? 1 : -1;
				for (int32 K = 1; K <= Count; ++K)
				{
					FIntVector Offset(0);
					Offset[Axis] = Sign * K;
					Visit(Offset, K);
				}
			}
			return;
		}
		const FIntVector Sign(Steps.X >= 0 ? 1 : -1, Steps.Y >= 0 ? 1 : -1, Steps.Z >= 0 ? 1 : -1);
		for (int32 Z = 0; Z <= FMath::Abs(Steps.Z); ++Z)
			for (int32 Y = 0; Y <= FMath::Abs(Steps.Y); ++Y)
				for (int32 X = 0; X <= FMath::Abs(Steps.X); ++X)
				{
					if (X == 0 && Y == 0 && Z == 0)
					{
						continue;
					}
					Visit(FIntVector(X * Sign.X, Y * Sign.Y, Z * Sign.Z), FMath::Max3(X, Y, Z));
				}
	}

	int64 CountReached(const FIntVector& Steps, bool bSeparateAxes)
	{
		const FIntVector A(FMath::Abs(Steps.X), FMath::Abs(Steps.Y), FMath::Abs(Steps.Z));
		return bSeparateAxes ? static_cast<int64>(A.X) + A.Y + A.Z
			: static_cast<int64>(A.X + 1) * (A.Y + 1) * (A.Z + 1) - 1;
	}
}

#if WITH_EDITOR
FText UPCGVoxelExtrudeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Extrude");
}

FText UPCGVoxelExtrudeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Grows or cuts each cell along Direction x Amount (each a constant, a per-cell point attribute or an @Data value), "
		"rounded up to whole cells per axis. Positive adds cells beyond the cell; negative removes the cell and that many "
		"beyond it. Separate Axes grows each axis as its own run; off grows the whole box. Added cells copy the cell they "
		"grew from and can be numbered by step (Extrude Step Attribute).");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelExtrudeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets to extrude. One output per input, keeping its metadata, tags and @Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelExtrudeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input extruded, still a valid voxel set: one point per cell, added cells copied from the cell "
			"they grew from (whole-cell bounds), removed cells gone. Is Surrounded is rewritten."));
	return Pins;
}

FPCGElementPtr UPCGVoxelExtrudeSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelExtrudeElement>();
}

bool FPCGVoxelExtrudeElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelExtrudePrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelExtrudeElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelExtrudeSettings* Settings = Context->GetInputSettings<UPCGVoxelExtrudeSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Extrude");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	const FVector& ConstantDirection = Settings->Direction.Constant;
	if (Settings->Direction.IsConstant()
		&& (!FMath::IsFinite(ConstantDirection.X) || !FMath::IsFinite(ConstantDirection.Y) || !FMath::IsFinite(ConstantDirection.Z)))
	{
		Fail(LOCTEXT("DirectionNotFinite", "Direction is not a finite vector."));
		return true;
	}
	if (Settings->Amount.IsConstant() && !FMath::IsFinite(Settings->Amount.Constant))
	{
		Fail(LOCTEXT("AmountNotFinite", "Amount is not a finite number."));
		return true;
	}
	if (Settings->bWriteExtrudeStep && Settings->ExtrudeStepAttribute.IsNone())
	{
		Fail(LOCTEXT("StepNameNone", "Extrude Step Attribute is empty while its output is on; give it a name or turn it off."));
		return true;
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
		if (Grid.NumCells() == 0)
		{
			Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]).Pin = PCGPinConstants::DefaultOutputLabel;
			continue;
		}

		TPCGUtilsPointValues<FVector> DirectionValues;
		TPCGUtilsPointValues<double> AmountValues;
		FText ReadError;
		if (!Settings->Direction.TryRead(Points, DirectionValues, ReadError, LOCTEXT("DirectionLabel", "Direction")))
		{
			Fail(ReadError, InputIndex);
			continue;
		}
		if (!Settings->Amount.TryRead(Points, AmountValues, ReadError, LOCTEXT("AmountLabel", "Amount")))
		{
			Fail(ReadError, InputIndex);
			continue;
		}

		// ---- Each point's signed steps per axis, and whether it adds or removes ----
		const int32 NumPoints = Points->GetNumPoints();
		TArray<FIntVector> PointSteps;
		TArray<int8> PointSign;
		PointSteps.SetNumZeroed(NumPoints);
		PointSign.SetNumZeroed(NumPoints);
		bool bBad = false;
		for (int32 Point = 0; Point < NumPoints && !bBad; ++Point)
		{
			const FVector Dir = DirectionValues.Get(Point);
			const double Value = AmountValues.Get(Point);
			if (Dir.ContainsNaN() || !FMath::IsFinite(Dir.X) || !FMath::IsFinite(Dir.Y) || !FMath::IsFinite(Dir.Z))
			{
				Fail(FText::Format(LOCTEXT("PointDirNotFinite", "on point {0}, Direction ({1}) is not a finite vector."), Point, FText::FromString(Dir.ToString())), InputIndex);
				bBad = true;
				break;
			}
			if (!FMath::IsFinite(Value))
			{
				Fail(FText::Format(LOCTEXT("PointAmountNotFinite", "on point {0}, Amount is not a finite number."), Point), InputIndex);
				bBad = true;
				break;
			}
			FIntVector Steps(0);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 Count = WholeSteps(FMath::Abs(Dir[Axis] * Value));
				Steps[Axis] = Dir[Axis] < 0.0 ? -Count : Count;
			}
			PointSteps[Point] = Steps;
			PointSign[Point] = Steps == FIntVector::ZeroValue ? 0 : (Value > 0.0 ? 1 : -1);
		}
		if (bBad)
		{
			continue;
		}

		// ---- Work estimate, before touching anything ----
		int64 Work = 0;
		for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
		{
			const int32 Point = Grid.CellPoint[Index];
			if (PointSign[Point] != 0)
			{
				Work += CountReached(PointSteps[Point], Settings->bSeparateAxes);
			}
		}
		if (Work > Settings->MaxCells)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the cells would add or remove {0} cells in total, over Max Cells ({1}). Raise Max Cells, "
				"or lower Direction x Amount."), FText::AsNumber(Work), FText::AsNumber(Settings->MaxCells)), InputIndex);
			continue;
		}

		// ---- Additions (from the input cells) and removals ----
		struct FAdded { int32 SourceCell; int32 Step; };
		TMap<FIntVector, FAdded> Added;
		TSet<FIntVector> Removed;
		for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
		{
			const int32 Point = Grid.CellPoint[Index];
			const int8 Sign = PointSign[Point];
			if (Sign == 0)
			{
				continue;
			}
			const FIntVector From = Grid.Cells[Index];
			if (Sign < 0)
			{
				Removed.Add(From);
				ForEachReached(PointSteps[Point], Settings->bSeparateAxes, [&](const FIntVector& Offset, int32)
				{
					Removed.Add(From + Offset);
				});
				continue;
			}
			ForEachReached(PointSteps[Point], Settings->bSeparateAxes, [&](const FIntVector& Offset, int32 Step)
			{
				const FIntVector Cell = From + Offset;
				if (Grid.IsOccupied(Cell))
				{
					return;
				}
				FAdded* Existing = Added.Find(Cell);
				if (!Existing)
				{
					Added.Add(Cell, { Index, Step });
				}
				else if (Step < Existing->Step)
				{
					*Existing = { Index, Step };
				}
			});
		}

		// ---- Result cells, in cell order (Z, Y, X) ----
		struct FResultCell { FIntVector Cell; int32 SourceCell; int32 Step; bool bAdded; };
		TArray<FResultCell> Result;
		Result.Reserve(Grid.NumCells() + Added.Num());
		for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
		{
			if (!Removed.Contains(Grid.Cells[Index]))
			{
				Result.Add({ Grid.Cells[Index], Index, 0, false });
			}
		}
		for (const TPair<FIntVector, FAdded>& Pair : Added)
		{
			if (!Removed.Contains(Pair.Key))
			{
				Result.Add({ Pair.Key, Pair.Value.SourceCell, Pair.Value.Step, true });
			}
		}
		Algo::Sort(Result, [](const FResultCell& A, const FResultCell& B)
		{
			if (A.Cell.Z != B.Cell.Z) { return A.Cell.Z < B.Cell.Z; }
			if (A.Cell.Y != B.Cell.Y) { return A.Cell.Y < B.Cell.Y; }
			return A.Cell.X < B.Cell.X;
		});
		TSet<FIntVector> Filled;
		Filled.Reserve(Result.Num());
		for (const FResultCell& Cell : Result)
		{
			Filled.Add(Cell.Cell);
		}

		// ---- Write: one point per cell; added cells get whole-cell bounds (and fresh entries for Added Values None) ----
		TArray<FPCGVoxelCellOut> Outs;
		Outs.Reserve(Result.Num());
		for (const FResultCell& Cell : Result)
		{
			Outs.Add({ Grid.CellPoint[Cell.SourceCell], Cell.Cell, FIntVector(1) });
		}
		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Outs, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false);
		{
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
			FPCGMetadataDomain* OutDomain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			const FQuat GridRotation = Grid.GridToWorld.GetRotation();
			const FVector Half = Grid.VoxelSize * 0.5;
			for (int32 Index = 0; Index < Result.Num(); ++Index)
			{
				if (!Result[Index].bAdded)
				{
					continue;
				}
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
				if (Settings->AddedValues == EPCGVoxelOffsetAddedValues::None && OutDomain)
				{
					Entries[Index] = OutDomain->AddEntry();
				}
			}
		}
		PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, Outs,
			[&Filled](const FIntVector& Cell) { return Filled.Contains(Cell); });
		if (Settings->bWriteExtrudeStep)
		{
			TArray<int32> Steps;
			Steps.SetNumUninitialized(Result.Num());
			for (int32 Index = 0; Index < Result.Num(); ++Index)
			{
				Steps[Index] = Result[Index].Step;
			}
			PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->ExtrudeStepAttribute, 0, Steps);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
