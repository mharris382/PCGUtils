// Copyright Max Harris

#include "Voxel/PCGVoxelGrid.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGVoxelGrid"

namespace PCGVoxelGridPrivate
{
	FText TypeName(int16 TypeId)
	{
		return FText::FromString(StaticEnum<EPCGMetadataTypes>()->GetNameStringByValue(TypeId));
	}

	FText CellText(const FIntVector& Cell)
	{
		return FText::FromString(FString::Printf(TEXT("(%d, %d, %d)"), Cell.X, Cell.Y, Cell.Z));
	}

	const TCHAR* AxisName(int32 Axis)
	{
		return Axis == 0 ? TEXT("X") : (Axis == 1 ? TEXT("Y") : TEXT("Z"));
	}

	const FPCGMetadataAttributeBase* FindAttribute(const UPCGBasePointData* Points, const FPCGMetadataDomainID& Domain, FName Name)
	{
		const UPCGMetadata* Metadata = Points ? Points->ConstMetadata() : nullptr;
		if (!Metadata || Name.IsNone())
		{
			return nullptr;
		}
		// Asking for an attribute in a domain the metadata never created logs an error, so ask for the domain first.
		const FPCGMetadataDomain* MetadataDomain = Metadata->GetConstMetadataDomain(Domain);
		return MetadataDomain ? MetadataDomain->GetConstAttribute(Name) : nullptr;
	}
}

FPCGVoxelGridReadSettings::FPCGVoxelGridReadSettings()
	: VoxelSize(FVector(100.0))
{
	VoxelSize.Input = EPCGUtilsInputValueType::Attribute;
	VoxelSize.Attribute.Update(TEXT("@Data.VoxelSize"));
}

namespace PCGVoxel
{
	FText FormatError(const FText& NodeTitle, int32 InputIndex, const FText& Message)
	{
		if (InputIndex == INDEX_NONE)
		{
			return FText::Format(LOCTEXT("ErrorNoInput", "{0}: {1}"), NodeTitle, Message);
		}
		return FText::Format(LOCTEXT("Error", "{0}: input {1}: {2}"), NodeTitle, FText::AsNumber(InputIndex), Message);
	}

	bool ToSpan(double Value, int32& OutSpan)
	{
		const double Rounded = FMath::RoundToDouble(Value);
		if (Rounded < 1.0 || FMath::Abs(Value - Rounded) > 1e-3 || Rounded > 1 << 20)
		{
			return false;
		}
		OutSpan = static_cast<int32>(Rounded);
		return true;
	}
}

bool FPCGVoxelGrid::ReadFrame(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGVoxelGridReadSettings& Settings,
	const FText& NodeTitle, int32 InputIndex, FVector& OutVoxelSize, FTransform& OutGridToWorld)
{
	using namespace PCGVoxelGridPrivate;

	FText Error;
	if (!Settings.VoxelSize.TryReadDataValue(Points, OutVoxelSize, Error, LOCTEXT("VoxelSizeLabel", "Voxel Size"), InputIndex))
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, INDEX_NONE, Error), Context);
		return false;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!(OutVoxelSize[Axis] > 0.0))
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				LOCTEXT("VoxelSizeAxis", "Voxel Size {0} is {1}; every axis must be greater than 0."),
				FText::FromString(AxisName(Axis)), FText::AsNumber(OutVoxelSize[Axis]))), Context);
			return false;
		}
	}

	if (const FPCGMetadataAttributeBase* Attribute = FindAttribute(Points, PCGMetadataDomainID::Data, Settings.GridTransformAttribute))
	{
		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<FTransform>::Id)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				LOCTEXT("GridTransformType", "@Data.{0} (Grid Transform Attribute) is of type {1}; it must be a Transform."),
				FText::FromName(Settings.GridTransformAttribute), TypeName(Attribute->GetTypeId()))), Context);
			return false;
		}
		OutGridToWorld = static_cast<const FPCGMetadataAttribute<FTransform>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey);
		if (!OutGridToWorld.GetScale3D().Equals(FVector::OneVector, 1e-4))
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				LOCTEXT("GridTransformScale", "@Data.{0} (Grid Transform Attribute) has scale {1}; it must be 1, because cell size comes from Voxel Size."),
				FText::FromName(Settings.GridTransformAttribute), FText::FromString(OutGridToWorld.GetScale3D().ToString()))), Context);
			return false;
		}
		OutGridToWorld.SetScale3D(FVector::OneVector);
		return true;
	}

	OutGridToWorld = FTransform::Identity;
	if (Settings.MissingGridTransform == EPCGVoxelMissingGridTransform::TargetActor)
	{
		const FTransform Actor = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, nullptr, /*bConvertToLocalSpace=*/true);
		OutGridToWorld = FTransform(Actor.GetRotation(), Actor.GetLocation());
	}
	return true;
}

bool FPCGVoxelGrid::Build(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGVoxelGridReadSettings& Settings,
	const FText& NodeTitle, int32 InputIndex, FPCGVoxelGrid& OutGrid)
{
	using namespace PCGVoxelGridPrivate;

	OutGrid = FPCGVoxelGrid();
	if (!Points)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, LOCTEXT("NotPoints", "the input is not point data.")), Context);
		return false;
	}
	if (!ReadFrame(Context, Points, Settings, NodeTitle, InputIndex, OutGrid.VoxelSize, OutGrid.GridToWorld))
	{
		return false;
	}

	const FPCGMetadataAttribute<FVector>* SpanAttribute = nullptr;
	if (const FPCGMetadataAttributeBase* Attribute = FindAttribute(Points, PCGMetadataDomainID::Elements, Settings.SpanAttribute))
	{
		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<FVector>::Id)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				LOCTEXT("SpanType", "point attribute '{0}' (Span Attribute) is of type {1}; it must be a Vector of whole numbers."),
				FText::FromName(Settings.SpanAttribute), TypeName(Attribute->GetTypeId()))), Context);
			return false;
		}
		SpanAttribute = static_cast<const FPCGMetadataAttribute<FVector>*>(Attribute);
		OutGrid.bChunked = true;
	}

	const int32 NumPoints = Points->GetNumPoints();
	const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
	const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
	const double Tolerance = Settings.GridTolerance * OutGrid.VoxelSize.GetMin();

	OutGrid.PointFirst.SetNumUninitialized(NumPoints);
	OutGrid.PointSpan.SetNumUninitialized(NumPoints);
	OutGrid.PointUsed.Init(true, NumPoints);

	// Pass 1: where each point sits, checked against the grid. Points are independent, so this runs in parallel;
	// the first failing point (lowest index) is then checked again serially to log its exact error.
	auto Evaluate = [&](int32 PointIndex, bool bLog) -> bool
	{
		FIntVector Span(1);
		if (SpanAttribute)
		{
			const FVector Value = SpanAttribute->GetValueFromItemKey(Entries[PointIndex]);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (!PCGVoxel::ToSpan(Value[Axis], Span[Axis]))
				{
					if (bLog)
					{
						PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
							LOCTEXT("SpanValue", "point {0}: '{1}' {2} is {3}; a span must be a whole number of cells, at least 1."),
							FText::AsNumber(PointIndex), FText::FromName(Settings.SpanAttribute), FText::FromString(AxisName(Axis)),
							FText::AsNumber(Value[Axis]))), Context);
					}
					return false;
				}
			}
		}

		// The point is the centre of its box of cells, so the first cell sits (Span - 1) / 2 cells below it.
		const FVector Local = OutGrid.GridToWorld.InverseTransformPositionNoScale(Transforms[PointIndex].GetLocation());
		const FVector FirstExact = Local / OutGrid.VoxelSize - (FVector(Span) - FVector(1.0)) * 0.5;
		const FIntVector First(FMath::RoundToInt(FirstExact.X), FMath::RoundToInt(FirstExact.Y), FMath::RoundToInt(FirstExact.Z));
		const double Distance = ((FirstExact - FVector(First)) * OutGrid.VoxelSize).Size();
		if (Distance > Tolerance && Settings.OffGridPoints == EPCGVoxelOffGridPoints::Error)
		{
			if (bLog)
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
					LOCTEXT("OffGrid", "point {0} is off the grid: its centre is {1} from where a {2} cell centre would be; Grid Tolerance allows {3} ({4} of the smallest Voxel Size axis)."),
					FText::AsNumber(PointIndex), FText::AsNumber(Distance),
					FText::FromString(Span == FIntVector(1) ? FString(TEXT("single")) : FString::Printf(TEXT("%dx%dx%d chunk's"), Span.X, Span.Y, Span.Z)),
					FText::AsNumber(Tolerance), FText::AsNumber(Settings.GridTolerance))), Context);
			}
			return false;
		}
		OutGrid.PointFirst[PointIndex] = First;
		OutGrid.PointSpan[PointIndex] = Span;
		return true;
	};
	TArray<bool> PointOk;
	PointOk.SetNumUninitialized(NumPoints);
	ParallelForTemplate(TEXT("PCGVoxelGrid.ReadPoints"), NumPoints, 8192, [&](int32 PointIndex)
	{
		PointOk[PointIndex] = Evaluate(PointIndex, false);
	});

	FIntVector Min(TNumericLimits<int32>::Max());
	FIntVector Max(TNumericLimits<int32>::Lowest());
	int64 NumCovered = 0;
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		if (!PointOk[PointIndex])
		{
			Evaluate(PointIndex, true);
			return false;
		}
		const FIntVector First = OutGrid.PointFirst[PointIndex];
		const FIntVector Span = OutGrid.PointSpan[PointIndex];
		const FIntVector Last = First + Span - FIntVector(1);
		Min = FIntVector(FMath::Min(Min.X, First.X), FMath::Min(Min.Y, First.Y), FMath::Min(Min.Z, First.Z));
		Max = FIntVector(FMath::Max(Max.X, Last.X), FMath::Max(Max.Y, Last.Y), FMath::Max(Max.Z, Last.Z));
		NumCovered += static_cast<int64>(Span.X) * Span.Y * Span.Z;
	}
	if (NumPoints == 0)
	{
		OutGrid.FinalizeCells();
		return true;
	}
	OutGrid.InitStorage(Min, Max, NumCovered);

	// Pass 2: claim cells in point order, settling clashes as Duplicate Cells says. Without chunks every point is
	// one cell, so a clash involves exactly one other point and needs no bookkeeping.
	if (!OutGrid.bChunked)
	{
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const FIntVector Cell = OutGrid.PointFirst[PointIndex];
			if (int32* Other = OutGrid.FindSlot(Cell))
			{
				if (Settings.DuplicateCells == EPCGVoxelDuplicateCells::Error)
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
						LOCTEXT("Duplicate", "points {0} and {1} both cover cell {2}; each cell can hold one point (Duplicate Cells is Error)."),
						FText::AsNumber(*Other), FText::AsNumber(PointIndex), CellText(Cell))), Context);
					return false;
				}
				if (Settings.DuplicateCells == EPCGVoxelDuplicateCells::KeepFirst)
				{
					OutGrid.PointUsed[PointIndex] = false;
				}
				else
				{
					OutGrid.PointUsed[*Other] = false;
					*Other = PointIndex;
				}
				continue;
			}
			OutGrid.SetSlot(Cell, PointIndex);
		}
		OutGrid.FinalizeCells();
		return true;
	}

	TArray<int32> Clashing;
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FIntVector First = OutGrid.PointFirst[PointIndex];
		const FIntVector Span = OutGrid.PointSpan[PointIndex];
		Clashing.Reset();
		for (int32 Z = 0; Z < Span.Z; ++Z)
		{
			for (int32 Y = 0; Y < Span.Y; ++Y)
			{
				for (int32 X = 0; X < Span.X; ++X)
				{
					const FIntVector Cell = First + FIntVector(X, Y, Z);
					if (const int32* Other = OutGrid.FindSlot(Cell))
					{
						if (Settings.DuplicateCells == EPCGVoxelDuplicateCells::Error)
						{
							PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
								LOCTEXT("Duplicate", "points {0} and {1} both cover cell {2}; each cell can hold one point (Duplicate Cells is Error)."),
								FText::AsNumber(*Other), FText::AsNumber(PointIndex), CellText(Cell))), Context);
							return false;
						}
						Clashing.AddUnique(*Other);
					}
				}
			}
		}

		if (!Clashing.IsEmpty())
		{
			if (Settings.DuplicateCells == EPCGVoxelDuplicateCells::KeepFirst)
			{
				OutGrid.PointUsed[PointIndex] = false;
				continue;
			}
			for (const int32 Other : Clashing)
			{
				OutGrid.PointUsed[Other] = false;
				const FIntVector OtherFirst = OutGrid.PointFirst[Other];
				const FIntVector OtherSpan = OutGrid.PointSpan[Other];
				for (int32 Z = 0; Z < OtherSpan.Z; ++Z)
				{
					for (int32 Y = 0; Y < OtherSpan.Y; ++Y)
					{
						for (int32 X = 0; X < OtherSpan.X; ++X)
						{
							OutGrid.ClearSlot(OtherFirst + FIntVector(X, Y, Z));
						}
					}
				}
			}
		}
		for (int32 Z = 0; Z < Span.Z; ++Z)
		{
			for (int32 Y = 0; Y < Span.Y; ++Y)
			{
				for (int32 X = 0; X < Span.X; ++X)
				{
					OutGrid.SetSlot(First + FIntVector(X, Y, Z), PointIndex);
				}
			}
		}
	}

	OutGrid.FinalizeCells();
	return true;
}

bool FPCGVoxelGrid::BuildCells(TPair<int32, int32>* OutClash)
{
	// Called from Build the storage is already filled; called by hand (tests, writers) it is filled here from the
	// point arrays.
	if (!bStorageReady)
	{
		if (PointUsed.Num() != PointFirst.Num())
		{
			PointUsed.Init(true, PointFirst.Num());
		}
		if (PointSpan.Num() != PointFirst.Num())
		{
			PointSpan.Init(FIntVector(1), PointFirst.Num());
		}
		FIntVector Min(TNumericLimits<int32>::Max());
		FIntVector Max(TNumericLimits<int32>::Lowest());
		int64 NumCovered = 0;
		for (int32 Point = 0; Point < PointFirst.Num(); ++Point)
		{
			if (!PointUsed[Point])
			{
				continue;
			}
			const FIntVector First = PointFirst[Point];
			const FIntVector Last = First + PointSpan[Point] - FIntVector(1);
			Min = FIntVector(FMath::Min(Min.X, First.X), FMath::Min(Min.Y, First.Y), FMath::Min(Min.Z, First.Z));
			Max = FIntVector(FMath::Max(Max.X, Last.X), FMath::Max(Max.Y, Last.Y), FMath::Max(Max.Z, Last.Z));
			NumCovered += static_cast<int64>(PointSpan[Point].X) * PointSpan[Point].Y * PointSpan[Point].Z;
		}
		if (NumCovered > 0)
		{
			InitStorage(Min, Max, NumCovered);
		}
		for (int32 Point = 0; Point < PointFirst.Num() && NumCovered > 0; ++Point)
		{
			if (!PointUsed[Point])
			{
				continue;
			}
			const FIntVector Span = PointSpan[Point];
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						const FIntVector Cell = PointFirst[Point] + FIntVector(X, Y, Z);
						if (const int32* Other = FindSlot(Cell))
						{
							if (OutClash)
							{
								*OutClash = TPair<int32, int32>(*Other, Point);
							}
							return false;
						}
						SetSlot(Cell, Point);
					}
				}
			}
		}
	}
	FinalizeCells();
	return true;
}

void FPCGVoxelGrid::InitStorage(const FIntVector& Min, const FIntVector& Max, int64 NumCovered)
{
	const FIntVector Dims = Max - Min + FIntVector(1);
	const int64 Volume = static_cast<int64>(Dims.X) * Dims.Y * Dims.Z;
	// Dense when the box is at most 8 times the cells it holds (or small anyway), and no more than 64M entries.
	constexpr int64 MaxDense = int64(1) << 26;
	bDense = Volume <= MaxDense && Volume <= FMath::Max<int64>(NumCovered * 8, 1 << 16);
	if (bDense)
	{
		DenseOrigin = Min;
		DenseDims = Dims;
		DenseCell.Init(INDEX_NONE, Volume);
		SparseCell.Empty();
	}
	else
	{
		DenseCell.Empty();
		SparseCell.Reserve(static_cast<int32>(FMath::Min<int64>(NumCovered, MAX_int32)));
	}
	bStorageReady = true;
}

int32* FPCGVoxelGrid::FindSlot(const FIntVector& Cell)
{
	if (bDense)
	{
		const FIntVector Local = Cell - DenseOrigin;
		int32& Slot = DenseCell[Local.X + static_cast<int64>(DenseDims.X) * (Local.Y + static_cast<int64>(DenseDims.Y) * Local.Z)];
		return Slot == INDEX_NONE ? nullptr : &Slot;
	}
	return SparseCell.Find(Cell);
}

void FPCGVoxelGrid::SetSlot(const FIntVector& Cell, int32 Value)
{
	if (bDense)
	{
		const FIntVector Local = Cell - DenseOrigin;
		DenseCell[Local.X + static_cast<int64>(DenseDims.X) * (Local.Y + static_cast<int64>(DenseDims.Y) * Local.Z)] = Value;
		return;
	}
	SparseCell.Add(Cell, Value);
}

void FPCGVoxelGrid::ClearSlot(const FIntVector& Cell)
{
	if (bDense)
	{
		SetSlot(Cell, INDEX_NONE);
		return;
	}
	SparseCell.Remove(Cell);
}

void FPCGVoxelGrid::FinalizeCells()
{
	Cells.Reset();
	CellPoint.Reset();
	if (bDense)
	{
		Cells.Reserve(PointFirst.Num());
		CellPoint.Reserve(PointFirst.Num());
		// Scanning the box in Z, Y, X order gives Cells already sorted.
		int64 Index = 0;
		for (int32 Z = 0; Z < DenseDims.Z; ++Z)
		{
			for (int32 Y = 0; Y < DenseDims.Y; ++Y)
			{
				for (int32 X = 0; X < DenseDims.X; ++X, ++Index)
				{
					int32& Slot = DenseCell[Index];
					if (Slot != INDEX_NONE)
					{
						CellPoint.Add(Slot);
						Slot = Cells.Add(DenseOrigin + FIntVector(X, Y, Z));
					}
				}
			}
		}
	}
	else
	{
		SparseCell.GenerateKeyArray(Cells);
		Cells.Sort([](const FIntVector& A, const FIntVector& B) { return CellLess(A, B); });
		CellPoint.SetNumUninitialized(Cells.Num());
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			int32& Slot = SparseCell.FindChecked(Cells[Index]);
			CellPoint[Index] = Slot;
			Slot = Index;
		}
	}
	bStorageReady = true;

	if (!Cells.IsEmpty())
	{
		MinCell = MaxCell = Cells[0];
		for (const FIntVector& Cell : Cells)
		{
			MinCell = FIntVector(FMath::Min(MinCell.X, Cell.X), FMath::Min(MinCell.Y, Cell.Y), FMath::Min(MinCell.Z, Cell.Z));
			MaxCell = FIntVector(FMath::Max(MaxCell.X, Cell.X), FMath::Max(MaxCell.Y, Cell.Y), FMath::Max(MaxCell.Z, Cell.Z));
		}
	}
}

void FPCGVoxelGrid::ComputeSurfaceOffsets(const UPCGBasePointData* Points, TArray<FVector>& OutMinSide, TArray<FVector>& OutMaxSide) const
{
	const int32 Num = NumPoints();
	OutMinSide.Init(FVector::ZeroVector, Num);
	OutMaxSide.Init(FVector::ZeroVector, Num);
	if (!Points || Points->GetNumPoints() != Num)
	{
		return;
	}
	const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
	const TConstPCGValueRange<FVector> Mins = Points->GetConstBoundsMinValueRange();
	const TConstPCGValueRange<FVector> Maxs = Points->GetConstBoundsMaxValueRange();
	for (int32 Point = 0; Point < Num; ++Point)
	{
		const FVector Scale = Transforms[Point].GetScale3D().GetAbs();
		const FVector Half = BoxHalfSize(PointSpan[Point]);
		OutMinSide[Point] = -Mins[Point] * Scale - Half;
		OutMaxSide[Point] = Maxs[Point] * Scale - Half;
	}
}

#undef LOCTEXT_NAMESPACE
