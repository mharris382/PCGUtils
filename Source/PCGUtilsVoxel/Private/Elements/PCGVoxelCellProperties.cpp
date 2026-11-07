// Copyright Max Harris

#include "Elements/PCGVoxelCellProperties.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelCellProperties"

namespace PCGVoxelCellPropertiesPrivate
{
	/** The six face directions in mask-bit order: +X, -X, +Y, -Y, +Z, -Z. */
	const FIntVector FaceStep[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };

	struct FUnionFind
	{
		TArray<int32> Parent;
		explicit FUnionFind(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Parent[Index] = Index;
			}
		}
		int32 Find(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};

	/** Neighbour steps for a connectivity, only one of each opposite pair (first non-zero component positive). */
	TArray<FIntVector> HalfNeighbourSteps(EPCGVoxelConnectivity Connectivity)
	{
		const int32 MaxNonZero = Connectivity == EPCGVoxelConnectivity::Faces ? 1 : (Connectivity == EPCGVoxelConnectivity::Edges ? 2 : 3);
		TArray<FIntVector> Steps;
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 X = -1; X <= 1; ++X)
				{
					const int32 NonZero = (X != 0) + (Y != 0) + (Z != 0);
					const int32 FirstNonZero = X != 0 ? X : (Y != 0 ? Y : Z);
					if (NonZero > 0 && NonZero <= MaxNonZero && FirstNonZero > 0)
					{
						Steps.Add(FIntVector(X, Y, Z));
					}
				}
			}
		}
		return Steps;
	}
}

#if WITH_EDITOR
FText UPCGVoxelCellPropertiesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Cell Properties");
}

FText UPCGVoxelCellPropertiesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Writes per-cell properties as point attributes: open sides (as a bitmask, count and direction vectors), whether the "
		"cell is surrounded, and optionally neighbour count, depth from the surface, cells above, sky visibility, connected "
		"components and the cell index. Each output has its own toggle and attribute name. A chunked set comes out expanded "
		"to one point per cell.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelCellPropertiesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets: point data whose points are occupied cells (or chunks) of the grid in @Data.VoxelSize."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelCellPropertiesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input with the enabled cell properties written; chunked inputs expanded to one point per cell."));
	return Pins;
}

FPCGElementPtr UPCGVoxelCellPropertiesSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelCellPropertiesElement>();
}

bool FPCGVoxelCellPropertiesElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelCellPropertiesPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelCellPropertiesElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelCellPropertiesSettings* Settings = Context->GetInputSettings<UPCGVoxelCellPropertiesSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Cell Properties");

	// Every enabled output needs a name of its own.
	struct FOutputName { bool bOn; FName Name; const TCHAR* Setting; };
	const FOutputName Names[] = {
		{ Settings->bWriteIsSurrounded, Settings->IsSurroundedAttribute, TEXT("Is Surrounded Attribute") },
		{ Settings->bWriteOpenSides, Settings->OpenSidesAttribute, TEXT("Open Sides Attribute") },
		{ Settings->bWriteOpenSideCount, Settings->OpenSideCountAttribute, TEXT("Open Side Count Attribute") },
		{ Settings->bWritePositiveOpenSides, Settings->PositiveOpenSidesAttribute, TEXT("Positive Open Sides Attribute") },
		{ Settings->bWriteNegativeOpenSides, Settings->NegativeOpenSidesAttribute, TEXT("Negative Open Sides Attribute") },
		{ Settings->bWriteOpenNormal, Settings->OpenNormalAttribute, TEXT("Open Normal Attribute") },
		{ Settings->bWriteOpenAxisCount, Settings->OpenAxisCountAttribute, TEXT("Open Axis Count Attribute") },
		{ Settings->bWriteNeighborCount, Settings->NeighborCountAttribute, TEXT("Neighbor Count Attribute") },
		{ Settings->bWriteDepth, Settings->DepthAttribute, TEXT("Depth Attribute") },
		{ Settings->bWriteCellsAbove, Settings->CellsAboveAttribute, TEXT("Cells Above Attribute") },
		{ Settings->bWriteIsSkyVisible, Settings->IsSkyVisibleAttribute, TEXT("Is Sky Visible Attribute") },
		{ Settings->bWriteComponentID, Settings->ComponentIDAttribute, TEXT("Component ID Attribute") },
		{ Settings->bWriteComponentSize, Settings->ComponentSizeAttribute, TEXT("Component Size Attribute") },
		{ Settings->bWriteCellIndex, Settings->CellIndexAttribute, TEXT("Cell Index Attribute") },
	};
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
					LOCTEXT("SameName", "{0} and {1} are both '{2}'; each enabled output needs its own attribute name."),
					FText::FromString(Names[A].Setting), FText::FromString(Names[B].Setting), FText::FromName(Names[A].Name))), Context);
				return true;
			}
		}
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		// One output point per cell, in point order (a chunk's cells in Z, Y, X order), copied from its point.
		TArray<FPCGVoxelCellOut> OutCells;
		OutCells.Reserve(Grid.NumCells());
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
						OutCells.Add({ Point, First + FIntVector(X, Y, Z), FIntVector(1) });
					}
				}
			}
		}
		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, OutCells, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false);
		if (!Out)
		{
			continue;
		}
		// WriteCells has already given every point its own metadata entry.

		const int32 NumCells = Grid.NumCells();
		const int32 NumOut = OutCells.Num();

		// Open-side mask of every grid cell, and the grid cell of every output point.
		TArray<uint8> CellMask;
		CellMask.SetNumUninitialized(NumCells);
		ParallelForTemplate(TEXT("PCGVoxelCellProperties.Mask"), NumCells, 4096, [&](int32 Cell)
		{
			uint8 Mask = 0;
			for (int32 Dir = 0; Dir < 6; ++Dir)
			{
				if (!Grid.IsOccupied(Grid.Cells[Cell] + FaceStep[Dir]))
				{
					Mask |= 1 << Dir;
				}
			}
			CellMask[Cell] = Mask;
		});
		TArray<int32> OutCell;
		OutCell.SetNumUninitialized(NumOut);
		ParallelForTemplate(TEXT("PCGVoxelCellProperties.OutCell"), NumOut, 4096, [&](int32 OutIndex)
		{
			OutCell[OutIndex] = Grid.FindCellIndex(OutCells[OutIndex].First);
		});
		auto MaskOf = [&](int32 OutIndex) -> uint32 { return CellMask[OutCell[OutIndex]]; };

		auto Write = [&](bool bOn, FName Name, auto Default, auto&& ValueOf)
		{
			if (!bOn)
			{
				return;
			}
			using T = decltype(Default);
			TArray<T> Values;
			Values.SetNumUninitialized(NumOut);
			ParallelForTemplate(TEXT("PCGVoxelCellProperties.Write"), NumOut, 4096, [&](int32 OutIndex) { Values[OutIndex] = ValueOf(OutIndex); });
			PCGVoxelPointIO::WritePointAttribute<T>(Out, Name, Default, Values);
		};

		Write(Settings->bWriteIsSurrounded, Settings->IsSurroundedAttribute, false,
			[&](int32 I) { return MaskOf(I) == 0; });
		Write(Settings->bWriteOpenSides, Settings->OpenSidesAttribute, int32(0),
			[&](int32 I) { return static_cast<int32>(MaskOf(I)); });
		Write(Settings->bWriteOpenSideCount, Settings->OpenSideCountAttribute, int32(0),
			[&](int32 I) { return static_cast<int32>(FMath::CountBits(MaskOf(I))); });
		Write(Settings->bWritePositiveOpenSides, Settings->PositiveOpenSidesAttribute, FVector::ZeroVector,
			[&](int32 I) { const uint32 M = MaskOf(I); return FVector((M & 1) ? 1.0 : 0.0, (M & 4) ? 1.0 : 0.0, (M & 16) ? 1.0 : 0.0); });
		Write(Settings->bWriteNegativeOpenSides, Settings->NegativeOpenSidesAttribute, FVector::ZeroVector,
			[&](int32 I) { const uint32 M = MaskOf(I); return FVector((M & 2) ? -1.0 : 0.0, (M & 8) ? -1.0 : 0.0, (M & 32) ? -1.0 : 0.0); });
		Write(Settings->bWriteOpenNormal, Settings->OpenNormalAttribute, FVector::ZeroVector, [&](int32 I)
		{
			const uint32 M = MaskOf(I);
			FVector Sum(0.0);
			for (int32 Dir = 0; Dir < 6; ++Dir)
			{
				if (M & (1u << Dir))
				{
					Sum += FVector(FaceStep[Dir]);
				}
			}
			if (Sum.IsNearlyZero())
			{
				return FVector::ZeroVector;
			}
			Sum.Normalize();
			return Settings->OpenNormalSpace == EPCGVoxelDirectionSpace::World ? Grid.GridToWorld.TransformVectorNoScale(Sum) : Sum;
		});
		Write(Settings->bWriteOpenAxisCount, Settings->OpenAxisCountAttribute, int32(0), [&](int32 I)
		{
			const uint32 M = MaskOf(I);
			return int32((M & 3) != 0) + int32((M & 12) != 0) + int32((M & 48) != 0);
		});
		Write(Settings->bWriteNeighborCount, Settings->NeighborCountAttribute, int32(0), [&](int32 I)
		{
			int32 Count = 0;
			const FIntVector Cell = OutCells[I].First;
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				for (int32 Y = -1; Y <= 1; ++Y)
				{
					for (int32 X = -1; X <= 1; ++X)
					{
						Count += ((X | Y | Z) != 0 && Grid.IsOccupied(Cell + FIntVector(X, Y, Z))) ? 1 : 0;
					}
				}
			}
			return Count;
		});

		if (Settings->bWriteDepth)
		{
			// Breadth-first from every cell with an open side.
			TArray<int32> Depth;
			Depth.Init(0, NumCells);
			TArray<int32> Queue;
			Queue.Reserve(NumCells);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (CellMask[Cell] != 0)
				{
					Depth[Cell] = 1;
					Queue.Add(Cell);
				}
			}
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 Cell = Queue[Head];
				for (int32 Dir = 0; Dir < 6; ++Dir)
				{
					const int32 Next = Grid.FindCellIndex(Grid.Cells[Cell] + FaceStep[Dir]);
					if (Next != INDEX_NONE && Depth[Next] == 0)
					{
						Depth[Next] = Depth[Cell] + 1;
						Queue.Add(Next);
					}
				}
			}
			Write(true, Settings->DepthAttribute, int32(0), [&](int32 I) { return Depth[OutCell[I]]; });
		}

		if (Settings->bWriteCellsAbove)
		{
			// Cells are ordered by Z, so walking them backwards meets the cell above first.
			TArray<int32> Above;
			Above.SetNumUninitialized(NumCells);
			for (int32 Cell = NumCells - 1; Cell >= 0; --Cell)
			{
				const int32 Up = Grid.FindCellIndex(Grid.Cells[Cell] + FIntVector(0, 0, 1));
				Above[Cell] = Up == INDEX_NONE ? 0 : Above[Up] + 1;
			}
			Write(true, Settings->CellsAboveAttribute, int32(0), [&](int32 I) { return Above[OutCell[I]]; });
		}

		if (Settings->bWriteIsSkyVisible)
		{
			TMap<FIntPoint, int32> TopZ;
			TopZ.Reserve(NumCells / 4);
			for (const FIntVector& Cell : Grid.Cells)
			{
				int32& Top = TopZ.FindOrAdd(FIntPoint(Cell.X, Cell.Y), Cell.Z);
				Top = FMath::Max(Top, Cell.Z);
			}
			TArray<bool> Sky;
			Sky.SetNumUninitialized(NumCells);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const FIntVector& C = Grid.Cells[Cell];
				Sky[Cell] = TopZ.FindChecked(FIntPoint(C.X, C.Y)) == C.Z;
			}
			Write(true, Settings->IsSkyVisibleAttribute, false, [&](int32 I) { return Sky[OutCell[I]]; });
		}

		if (Settings->bWriteComponentID || Settings->bWriteComponentSize)
		{
			const TArray<FIntVector> Steps = HalfNeighbourSteps(Settings->Connectivity);
			FUnionFind Components(NumCells);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				for (const FIntVector& Step : Steps)
				{
					const int32 Other = Grid.FindCellIndex(Grid.Cells[Cell] + Step);
					if (Other != INDEX_NONE)
					{
						Components.Union(Cell, Other);
					}
				}
			}
			// Numbered in cell order, so the result does not depend on the union order.
			TArray<int32> RootID;
			RootID.Init(INDEX_NONE, NumCells);
			TArray<int32> ComponentOf;
			ComponentOf.SetNumUninitialized(NumCells);
			TArray<int32> Size;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const int32 Root = Components.Find(Cell);
				if (RootID[Root] == INDEX_NONE)
				{
					RootID[Root] = Size.Add(0);
				}
				ComponentOf[Cell] = RootID[Root];
				++Size[RootID[Root]];
			}
			Write(Settings->bWriteComponentID, Settings->ComponentIDAttribute, int32(0), [&](int32 I) { return ComponentOf[OutCell[I]]; });
			Write(Settings->bWriteComponentSize, Settings->ComponentSizeAttribute, int32(0), [&](int32 I) { return Size[ComponentOf[OutCell[I]]]; });
		}

		Write(Settings->bWriteCellIndex, Settings->CellIndexAttribute, FVector::ZeroVector,
			[&](int32 I) { return FVector(OutCells[I].First); });

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
