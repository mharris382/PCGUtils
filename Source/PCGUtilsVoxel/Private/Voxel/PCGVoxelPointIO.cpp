// Copyright Max Harris

#include "Voxel/PCGVoxelPointIO.h"

#include "Async/ParallelFor.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"

bool PCGVoxelPointIO::HasPointAttribute(const UPCGBasePointData* Points, FName Name)
{
	const UPCGMetadata* Metadata = Points ? Points->ConstMetadata() : nullptr;
	if (!Metadata || Name.IsNone())
	{
		return false;
	}
	const FPCGMetadataDomain* Domain = Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
	return Domain && Domain->HasAttribute(Name);
}

void PCGVoxelPointIO::MakeEntriesUnique(UPCGBasePointData* Points)
{
	if (!Points || Points->GetNumPoints() == 0)
	{
		return;
	}
	TPCGValueRange<int64> Entries = Points->GetMetadataEntryValueRange();
	TArray<int64*> Pointers;
	Pointers.SetNumUninitialized(Entries.Num());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		Pointers[Index] = &Entries[Index];
	}
	Points->MutableMetadata()->AddEntriesInPlace(Pointers);
}

UPCGBasePointData* PCGVoxelPointIO::WriteCells(FPCGContext* Context, const UPCGBasePointData* Source, const FPCGVoxelGrid& Grid,
	TConstArrayView<FPCGVoxelCellOut> Cells, FName SpanAttribute, bool bWriteSpan)
{
	UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
	Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Source));
	const int32 Num = Cells.Num();
	Out->SetNumPoints(Num, /*bInitializeValues=*/false);
	Out->AllocateProperties(Source->GetAllocatedProperties() | EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin
		| EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);

	TArray<int32> ReadIndices;
	TArray<int32> WriteIndices;
	ReadIndices.SetNumUninitialized(Num);
	WriteIndices.SetNumUninitialized(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		ReadIndices[Index] = Cells[Index].SourcePoint;
		WriteIndices[Index] = Index;
	}
	Source->CopyPointsTo(Out, ReadIndices, WriteIndices);

	TArray<FVector> OffsetMin, OffsetMax;
	Grid.ComputeSurfaceOffsets(Source, OffsetMin, OffsetMax);

	TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
	TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
	TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
	TPCGValueRange<int32> Seeds = Out->GetSeedValueRange();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FPCGVoxelCellOut& Cell = Cells[Index];
		const int32 From = Cell.SourcePoint;
		FTransform& Transform = Transforms[Index];
		Transform.SetLocation(Grid.GridToWorld.TransformPositionNoScale(Grid.BoxCenterGrid(Cell.First, Cell.Span)));

		FVector Scale = Transform.GetScale3D().GetAbs();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Scale[Axis] < UE_SMALL_NUMBER)
			{
				Scale[Axis] = 1.0;
			}
		}
		const FVector Half = Grid.BoxHalfSize(Cell.Span);
		Mins[Index] = -(Half + OffsetMin[From]) / Scale;
		Maxs[Index] = (Half + OffsetMax[From]) / Scale;

		const bool bUnchanged = Cell.First == Grid.PointFirst[From] && Cell.Span == Grid.PointSpan[From];
		if (!bUnchanged)
		{
			Seeds[Index] = PCGHelpers::ComputeSeed(Cell.First.X, Cell.First.Y, Cell.First.Z);
		}
	}

	MakeEntriesUnique(Out);
	if (bWriteSpan)
	{
		TArray<FVector> Spans;
		Spans.SetNumUninitialized(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Spans[Index] = FVector(Cells[Index].Span);
		}
		WritePointAttribute<FVector>(Out, SpanAttribute, FVector(1.0), Spans);
	}
	else if (!SpanAttribute.IsNone())
	{
		if (FPCGMetadataDomain* Domain = Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements))
		{
			if (Domain->HasAttribute(SpanAttribute))
			{
				Domain->DeleteAttribute(SpanAttribute);
			}
		}
	}
	return Out;
}

FPCGVoxelCellSet::FPCGVoxelCellSet(TConstArrayView<FPCGVoxelCellOut> Boxes)
{
	int64 Count = 0;
	for (const FPCGVoxelCellOut& Box : Boxes)
	{
		Count += static_cast<int64>(Box.Span.X) * Box.Span.Y * Box.Span.Z;
	}
	Cells.Reserve(static_cast<int32>(FMath::Min<int64>(Count, MAX_int32)));
	for (const FPCGVoxelCellOut& Box : Boxes)
	{
		for (int32 Z = 0; Z < Box.Span.Z; ++Z)
			for (int32 Y = 0; Y < Box.Span.Y; ++Y)
				for (int32 X = 0; X < Box.Span.X; ++X)
				{
					Cells.Add(Box.First + FIntVector(X, Y, Z));
				}
	}
}

bool PCGVoxelPointIO::IsBoxSurrounded(const FIntVector& First, const FIntVector& Span, TFunctionRef<bool(const FIntVector&)> IsFilled)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 U = (Axis + 1) % 3;
		const int32 V = (Axis + 2) % 3;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FIntVector Cell;
			Cell[Axis] = Side == 0 ? First[Axis] - 1 : First[Axis] + Span[Axis];
			for (int32 A = 0; A < Span[U]; ++A)
			{
				for (int32 B = 0; B < Span[V]; ++B)
				{
					Cell[U] = First[U] + A;
					Cell[V] = First[V] + B;
					if (!IsFilled(Cell))
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

bool PCGVoxelPointIO::RefreshSurrounded(const UPCGBasePointData* Source, UPCGBasePointData* Out, FName Name,
	TConstArrayView<FPCGVoxelCellOut> Cells, TFunctionRef<bool(const FIntVector&)> IsFilled)
{
	if (Name.IsNone() || !Source || !Out || Out->GetNumPoints() != Cells.Num())
	{
		return false;
	}
	const UPCGMetadata* Metadata = Source->ConstMetadata();
	const FPCGMetadataDomain* Domain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
	const FPCGMetadataAttributeBase* Attribute = Domain ? Domain->GetConstAttribute(Name) : nullptr;
	if (!Attribute || Attribute->GetTypeId() != PCG::Private::MetadataTypes<bool>::Id)
	{
		return false;
	}
	TArray<bool> Values;
	Values.SetNumUninitialized(Cells.Num());
	ParallelFor(TEXT("PCGVoxelPointIO.RefreshSurrounded"), Cells.Num(), 1024, [&](int32 Index)
	{
		Values[Index] = IsBoxSurrounded(Cells[Index].First, Cells[Index].Span, IsFilled);
	});
	WritePointAttribute<bool>(Out, Name, false, Values);
	return true;
}
