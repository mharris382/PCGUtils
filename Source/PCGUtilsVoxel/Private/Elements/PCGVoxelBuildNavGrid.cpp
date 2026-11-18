// Copyright Max Harris

#include "Elements/PCGVoxelBuildNavGrid.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Algo/Unique.h"
#include "Data/PCGUtilsClusterInterop.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelChunking.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelMorphology.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelBuildNavGrid"

#if WITH_EDITOR
FText UPCGVoxelBuildNavGridSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Build Nav Grid");
}

FText UPCGVoxelBuildNavGridSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Builds a navigation graph from a voxel set as a PCGEx cluster (Vtx and Edges, one Edges data per connected island), "
		"ready for PCGEx pathfinding and cluster nodes. Flying: a vertex wherever the agent fits in empty space with Surface "
		"Clearance from the filled cells.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelBuildNavGridSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGVoxelBuildNavGridPins::VoxelGrid, EPCGDataType::Point, true, true,
		LOCTEXT("GridTooltip", "Voxel sets to build nav grids for. One cluster (a Vtx data and its Edges datas) per input."))
		.SetRequiredPin();
	Pins.Emplace(PCGVoxelBuildNavGridPins::Bounds, EPCGDataType::Spatial, true, true,
		LOCTEXT("BoundsTooltip", "Optional. Where vertices may be: only cells whose vertex position lies inside it (density above 0). "
			"Replaces the padded box. One data for every input, or one per input."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelBuildNavGridSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGVoxelBuildNavGridPins::Vtx, EPCGDataType::Point, true, true,
		LOCTEXT("VtxTooltip", "PCGEx cluster vertices (tagged PCGEx/Vtx, attribute PCGEx/VData), one data per input. Bounds are the agent's box."));
	Pins.Emplace(PCGVoxelBuildNavGridPins::Edges, EPCGDataType::Point, true, true,
		LOCTEXT("EdgesTooltip", "PCGEx cluster edges (tagged PCGEx/Edges, attribute PCGEx/EData): one data per connected island, "
			"each paired with its input's Vtx data by the PCGEx/Cluster tag. Each point sits midway along its edge."));
	return Pins;
}

FPCGElementPtr UPCGVoxelBuildNavGridSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelBuildNavGridElement>();
}

namespace PCGVoxelBuildNavGridPrivate
{
	void AxisPriorityOf(EPCGUtilsAxisOrder Order, int32 Out[3])
	{
		static const int32 Orders[6][3] = { { 0, 1, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 1, 0, 2 }, { 2, 1, 0 }, { 0, 2, 1 } };
		const int32 Index = FMath::Clamp(static_cast<int32>(Order), 0, 5);
		Out[0] = Orders[Index][0];
		Out[1] = Orders[Index][1];
		Out[2] = Orders[Index][2];
	}

	/** A graph vertex in cell space: position, half size of its bounds, the box it stands for, aerial, portal. */
	struct FNavVertex
	{
		FVector CellPos;
		FVector HalfCells;
		FVector ChunkSize;
		bool bAerial = false;
		bool bPortal = false;
	};

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
}

bool FPCGVoxelBuildNavGridElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelBuildNavGridElement::ExecuteInternal);
	using namespace PCGVoxelBuildNavGridPrivate;
	check(Context);

	const UPCGVoxelBuildNavGridSettings* Settings = Context->GetInputSettings<UPCGVoxelBuildNavGridSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Build Nav Grid");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	if (Settings->AgentNavType == EPCGVoxelNavAgentType::Surface)
	{
		Fail(LOCTEXT("SurfaceNotBuilt", "Agent Nav Type is Surface, which is planned but not built yet; use Flying."));
		return true;
	}

	// Every enabled output needs a name of its own, and none may be a PCGEx cluster attribute.
	struct FNamedOutput { bool bOn; FName Name; FText Label; };
	const FNamedOutput Outputs[] = {
		{ Settings->bOutputSurfaceDistance, Settings->SurfaceDistanceAttribute, LOCTEXT("SurfaceDistanceLabel", "Surface Distance Attribute") },
		{ Settings->bOutputNearestVoxelIndex, Settings->NearestVoxelIndexAttribute, LOCTEXT("NearestLabel", "Nearest Voxel Index Attribute") },
		{ Settings->bOutputIsAerial, Settings->IsAerialAttribute, LOCTEXT("IsAerialLabel", "Is Aerial Attribute") },
		{ Settings->bOutputIsPortal, Settings->IsPortalAttribute, LOCTEXT("IsPortalLabel", "Is Portal Attribute") },
		{ Settings->bOutputChunkSize, Settings->ChunkSizeAttribute, LOCTEXT("ChunkSizeLabel", "Chunk Size Attribute") },
	};
	for (int32 A = 0; A < UE_ARRAY_COUNT(Outputs); ++A)
	{
		if (!Outputs[A].bOn)
		{
			continue;
		}
		if (Outputs[A].Name.IsNone())
		{
			Fail(FText::Format(LOCTEXT("EmptyName", "{0} is empty while its output is on; give it a name or turn the output off."), Outputs[A].Label));
			return true;
		}
		if (Outputs[A].Name == PCGUtilsClusterInterop::VtxDataAttribute)
		{
			Fail(FText::Format(LOCTEXT("ReservedName", "{0} is '{1}', the attribute PCGEx reads the cluster from; choose another name."),
				Outputs[A].Label, FText::FromName(Outputs[A].Name)));
			return true;
		}
		for (int32 B = A + 1; B < UE_ARRAY_COUNT(Outputs); ++B)
		{
			if (Outputs[B].bOn && Outputs[B].Name == Outputs[A].Name)
			{
				Fail(FText::Format(LOCTEXT("SameName", "{0} and {1} are both '{2}'; each enabled output needs its own attribute name."),
					Outputs[A].Label, Outputs[B].Label, FText::FromName(Outputs[A].Name)));
				return true;
			}
		}
	}
	if (Settings->Padding < 0)
	{
		Fail(FText::Format(LOCTEXT("NegativePadding", "Padding is {0}; it must be 0 or greater."), Settings->Padding));
		return true;
	}

	if (Settings->HubCubes < 0)
	{
		Fail(FText::Format(LOCTEXT("NegativeHubCubes", "Hub Cubes is {0}; it must be 0 or greater."), Settings->HubCubes));
		return true;
	}
	if (Settings->HubSubdivisions < 1)
	{
		Fail(FText::Format(LOCTEXT("BadHubSubdivisions", "Hub Subdivisions is {0}; it must be 1 or greater."), Settings->HubSubdivisions));
		return true;
	}
	if (Settings->AerialCellDistance < 0)
	{
		Fail(FText::Format(LOCTEXT("NegativeAerial", "Aerial Cell Distance is {0}; it must be 0 or greater."), Settings->AerialCellDistance));
		return true;
	}
	if (Settings->bEnableCompression && Settings->bCollapseAerialCells)
	{
		static const TCHAR* CollapseAxes[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Settings->AerialCellCollapseSize[Axis] < 1)
			{
				Fail(FText::Format(LOCTEXT("BadCollapse", "Aerial Cell Collapse Size {0} is {1}; each axis must be 1 or greater."),
					FText::FromString(CollapseAxes[Axis]), Settings->AerialCellCollapseSize[Axis]));
				return true;
			}
		}
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGVoxelBuildNavGridPins::VoxelGrid);
	const TArray<FPCGTaggedData> BoundsInputs = Context->InputData.GetInputsByPin(PCGVoxelBuildNavGridPins::Bounds);
	if (BoundsInputs.Num() > 1 && BoundsInputs.Num() != Inputs.Num())
	{
		Fail(FText::Format(LOCTEXT("BoundsCount", "Bounds has {0} datas and Voxel Grid has {1}; Bounds needs one data for every input, or one per input."),
			BoundsInputs.Num(), Inputs.Num()));
		return true;
	}
	int32 AxisPriority[3];
	AxisPriorityOf(Settings->NearestVoxelAxisPriority, AxisPriority);

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		if (!Points)
		{
			Fail(LOCTEXT("NotPoints", "the input is not point data; it must be a voxel set."), InputIndex);
			continue;
		}
		const UPCGSpatialData* Bounds = nullptr;
		if (!BoundsInputs.IsEmpty())
		{
			const FPCGTaggedData& BoundsInput = BoundsInputs[BoundsInputs.Num() == 1 ? 0 : InputIndex];
			Bounds = Cast<const UPCGSpatialData>(BoundsInput.Data);
			if (!Bounds)
			{
				Fail(LOCTEXT("BoundsNotSpatial", "the Bounds data for this input is not spatial data."), InputIndex);
				continue;
			}
		}
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid))
		{
			continue;
		}
		if (Grid.NumCells() == 0 && !Bounds)
		{
			continue; // nothing to fly round and no box to fly in
		}

		// ---- Agent ----
		FVector AgentSize;
		FText Error;
		if (!Settings->AgentSize.TryReadDataValue(Points, AgentSize, Error, LOCTEXT("AgentSizeLabel", "Agent Size"), InputIndex))
		{
			Fail(Error, InputIndex);
			continue;
		}
		static const TCHAR* AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		FIntVector ReachLo(0), ReachHi(0), AgentCells(1);
		bool bAgentOk = true;
		for (int32 Axis = 0; Axis < 3 && bAgentOk; ++Axis)
		{
			if (!FMath::IsFinite(AgentSize[Axis]) || AgentSize[Axis] <= 0.0)
			{
				Fail(FText::Format(LOCTEXT("BadAgentSize", "Agent Size {0} is {1}; each axis must be a number greater than 0."),
					FText::FromString(AxisNames[Axis]), FText::AsNumber(AgentSize[Axis])), InputIndex);
				bAgentOk = false;
				break;
			}
			AgentCells[Axis] = FMath::Max(1, FMath::CeilToInt32(AgentSize[Axis] - 1e-6));
			ReachLo[Axis] = (AgentCells[Axis] - 1) / 2;
			ReachHi[Axis] = AgentCells[Axis] - 1 - ReachLo[Axis];
		}
		if (!bAgentOk)
		{
			continue;
		}
		const FVector AgentOffset = FVector(ReachHi - ReachLo) * 0.5; // cells from the vertex's cell centre to the agent's centre

		int32 CompressionCells = 1;
		if (Settings->bEnableCompression)
		{
			double Compression = 0.0;
			if (!Settings->CompressionSize.TryReadDataValue(Points, Compression, Error, LOCTEXT("CompressionLabel", "Compression Size"), InputIndex))
			{
				Fail(Error, InputIndex);
				continue;
			}
			if (!FMath::IsFinite(Compression) || Compression < 1.0)
			{
				Fail(FText::Format(LOCTEXT("BadCompression", "Compression Size is {0}; it must be 1 or greater."), FText::AsNumber(Compression)), InputIndex);
				continue;
			}
			CompressionCells = FMath::Max(1, FMath::RoundToInt32(Compression));
		}

		TPCGUtilsPointValues<double> Clearance;
		if (!Settings->SurfaceClearance.TryRead(Points, Clearance, Error, LOCTEXT("ClearanceLabel", "Surface Clearance"), InputIndex))
		{
			Fail(Error, InputIndex);
			continue;
		}
		double MaxClearance = Clearance.Single;
		if (Clearance.IsPerPoint())
		{
			MaxClearance = 0.0;
			for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
			{
				if (Grid.PointUsed[Point])
				{
					MaxClearance = FMath::Max(MaxClearance, Clearance.PerPoint[Point]);
				}
			}
		}

		// ---- The box of cells vertices may occupy ----
		FIntVector WorkLo, WorkHi;
		if (Bounds)
		{
			const FBox World = Bounds->GetBounds();
			if (!World.IsValid)
			{
				Fail(LOCTEXT("BoundsEmpty", "the Bounds data for this input has empty bounds, so no cell can be inside it."), InputIndex);
				continue;
			}
			FBox Cells(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector P((Corner & 1) ? World.Max.X : World.Min.X, (Corner & 2) ? World.Max.Y : World.Min.Y, (Corner & 4) ? World.Max.Z : World.Min.Z);
				Cells += Grid.GridToWorld.InverseTransformPositionNoScale(P) / Grid.VoxelSize;
			}
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				WorkLo[Axis] = FMath::CeilToInt32(Cells.Min[Axis] - 0.5);
				WorkHi[Axis] = FMath::FloorToInt32(Cells.Max[Axis] + 0.5);
			}
			if (WorkHi.X < WorkLo.X || WorkHi.Y < WorkLo.Y || WorkHi.Z < WorkLo.Z)
			{
				continue; // thinner than a cell: no vertex fits
			}
		}
		else
		{
			WorkLo = Grid.MinCell - FIntVector(Settings->Padding);
			WorkHi = Grid.MaxCell + FIntVector(Settings->Padding);
		}
		FIntVector DomainLo = WorkLo, DomainHi = WorkHi;
		if (Grid.NumCells() > 0)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				DomainLo[Axis] = FMath::Min(DomainLo[Axis], Grid.MinCell[Axis]);
				DomainHi[Axis] = FMath::Max(DomainHi[Axis], Grid.MaxCell[Axis]);
			}
		}
		// Room for clearance stamps, the agent's box, the gap closing and one clear layer for the outside flood.
		const int32 PadCells = FMath::CeilToInt32(MaxClearance) + 2 + FMath::Max3(ReachLo.GetMax(), ReachHi.GetMax(), 0)
			+ (Settings->bIgnoreGaps ? 2 * FMath::Max(1, Settings->GapRadius) : 0) + 1;
		const FIntVector DomainSize = DomainHi - DomainLo + FIntVector(1 + 2 * PadCells);
		const int64 BoxCells = static_cast<int64>(DomainSize.X) * DomainSize.Y * DomainSize.Z;
		if (BoxCells > Settings->MaxCells || BoxCells >= MAX_int32)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to work over is {0} x {1} x {2} = {3} cells (the set and the space to fly in, "
				"plus room for clearance, the agent and gaps), over Max Cells ({4}, and under 2^31 at most). Raise Max Cells, shrink the Bounds or Padding, or split the set."),
				DomainSize.X, DomainSize.Y, DomainSize.Z, FText::AsNumber(BoxCells), FText::AsNumber(Settings->MaxCells)), InputIndex);
			continue;
		}

		// ---- Filled, blocked, and where the agent fits ----
		FPCGVoxelDenseMask Solid;
		Solid.Init(DomainLo, DomainHi, FIntVector(PadCells));
		TArray<int32> PointOfDense;
		PointOfDense.Init(INDEX_NONE, static_cast<int32>(Solid.Num()));
		for (int32 Index = 0; Index < Grid.NumCells(); ++Index)
		{
			const int64 I = Solid.Index(Grid.Cells[Index]);
			Solid.Bits[I] = 1;
			PointOfDense[I] = Grid.CellPoint[Index];
		}
		FPCGVoxelDenseMask Filled;
		if (Settings->bIgnoreGaps)
		{
			PCGVoxelMorphology::Close(Solid, FIntVector(FMath::Max(1, Settings->GapRadius)), Filled);
		}
		else
		{
			Filled = Solid;
		}
		auto RadiusOf = [&](int64 I) -> double
		{
			const int32 Point = PointOfDense[I];
			if (Point != INDEX_NONE)
			{
				return Clearance.Get(Point);
			}
			// A gap Ignore Gaps filled: the widest clearance of the filled points around it.
			double Radius = 0.0;
			const FIntVector Cell = Solid.Cell(I);
			for (int32 Z = -1; Z <= 1; ++Z)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 X = -1; X <= 1; ++X)
					{
						const FIntVector Other = Cell + FIntVector(X, Y, Z);
						if (Solid.Contains(Other) && PointOfDense[Solid.Index(Other)] != INDEX_NONE)
						{
							Radius = FMath::Max(Radius, Clearance.Get(PointOfDense[Solid.Index(Other)]));
						}
					}
			return Radius;
		};
		FPCGVoxelDenseMask Blocked;
		PCGVoxelMorphology::DilateByDistance(Filled, RadiusOf, Blocked);
		FPCGVoxelDenseMask Clear;
		Clear.InitLike(Blocked);
		for (int64 I = 0; I < Clear.Num(); ++I)
		{
			Clear.Bits[I] = Blocked.Bits[I] ? 0 : 1;
		}
		FPCGVoxelDenseMask Fits;
		PCGVoxelMorphology::ErodeBox(Clear, ReachLo, ReachHi, Fits);
		FPCGVoxelDenseMask Outside;
		if (Settings->FlyingSpace != EPCGVoxelNavFlyingSpace::PaddedBox)
		{
			PCGVoxelMorphology::FloodOutside(Filled, Outside);
		}

		auto VertexPosition = [&](const FVector& CellPos)
		{
			return Grid.GridToWorld.TransformPositionNoScale((CellPos + AgentOffset) * Grid.VoxelSize);
		};

		// Aerial cells: at least Aerial Cell Distance (box to box) from every filled cell.
		const bool bCompress = Settings->bEnableCompression;
		const bool bNeedAerial = Settings->bOutputIsAerial || (bCompress && Settings->bCollapseAerialCells);
		FPCGVoxelDenseMask Near;
		if (bNeedAerial)
		{
			const double AerialDistance = static_cast<double>(Settings->AerialCellDistance);
			PCGVoxelMorphology::DilateByDistance(Filled, [AerialDistance](int64) { return AerialDistance; }, Near);
		}
		auto IsAerialCell = [&](int64 I) { return bNeedAerial && !Near.Bits[I] && !Filled.Bits[I]; };

		// ---- Cells the agent may occupy ----
		TArray<int64> VtxDense;
		for (int32 Z = WorkLo.Z; Z <= WorkHi.Z; ++Z)
			for (int32 Y = WorkLo.Y; Y <= WorkHi.Y; ++Y)
				for (int32 X = WorkLo.X; X <= WorkHi.X; ++X)
				{
					const FIntVector Cell(X, Y, Z);
					const int64 I = Solid.Index(Cell);
					if (!Fits.Bits[I])
					{
						continue;
					}
					if (Settings->FlyingSpace == EPCGVoxelNavFlyingSpace::EnclosedOnly && Outside.Bits[I])
					{
						continue;
					}
					if (Settings->FlyingSpace == EPCGVoxelNavFlyingSpace::OutsideOnly && !Outside.Bits[I])
					{
						continue;
					}
					if (Bounds && !(Bounds->GetDensityAtPosition(VertexPosition(FVector(Cell))) > 0.0f))
					{
						continue;
					}
					VtxDense.Add(I);
				}

		// ---- The graph: vertices in cell space, edges between them ----
		TArray<FNavVertex> Verts;
		TArray<FIntVector2> EdgeList;
		if (!bCompress)
		{
			// One vertex per cell; each neighbour once (the positive half of the offsets), no corner cut on a diagonal.
			TArray<int32> VtxOfDense;
			VtxOfDense.Init(INDEX_NONE, static_cast<int32>(Solid.Num()));
			Verts.Reserve(VtxDense.Num());
			for (const int64 I : VtxDense)
			{
				VtxOfDense[I] = Verts.Add({ FVector(Solid.Cell(I)), FVector(AgentCells) * 0.5, FVector(1.0), IsAerialCell(I) });
			}
			TArray<FIntVector> Offsets;
			for (int32 Z = -1; Z <= 1; ++Z)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 X = -1; X <= 1; ++X)
					{
						const FIntVector D(X, Y, Z);
						const int32 Moved = FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z);
						const bool bPositive = Z > 0 || (Z == 0 && (Y > 0 || (Y == 0 && X > 0)));
						if (bPositive && (Moved == 1 || (Settings->bEnableDiagonals && Moved > 1)))
						{
							Offsets.Add(D);
						}
					}
			auto IsVtx = [&](const FIntVector& Cell) { return Solid.Contains(Cell) && VtxOfDense[Solid.Index(Cell)] != INDEX_NONE; };
			for (int32 V = 0; V < VtxDense.Num(); ++V)
			{
				const FIntVector Cell = Solid.Cell(VtxDense[V]);
				for (const FIntVector& D : Offsets)
				{
					if (!IsVtx(Cell + D))
					{
						continue;
					}
					// Every cell of the box the move spans must be a vertex too.
					bool bClear = true;
					for (int32 Corner = 1; Corner < 7 && bClear; ++Corner)
					{
						const FIntVector Part((Corner & 1) ? D.X : 0, (Corner & 2) ? D.Y : 0, (Corner & 4) ? D.Z : 0);
						if (Part != FIntVector::ZeroValue && Part != D)
						{
							bClear = IsVtx(Cell + Part);
						}
					}
					if (bClear)
					{
						EdgeList.Add(FIntVector2(V, VtxOfDense[Solid.Index(Cell + D)]));
					}
				}
			}
		}
		else
		{
			// Free boxes (shared greedy chunking); aerial and near cells never share a box.
			TArray<FIntVector> FreeCells;
			TArray<int32> FreeLabels;
			FreeCells.Reserve(VtxDense.Num());
			FreeLabels.Reserve(VtxDense.Num());
			for (const int64 I : VtxDense)
			{
				FreeCells.Add(Solid.Cell(I));
				FreeLabels.Add(IsAerialCell(I) ? 1 : 0);
			}
			const bool bCollapse = Settings->bCollapseAerialCells;
			auto MaxSpanOf = [&](int32 Label) { return (bCollapse && Label == 1) ? Settings->AerialCellCollapseSize : FIntVector(CompressionCells); };
			TArray<PCGVoxelChunking::FBox> Boxes;
			PCGVoxelChunking::GrowBoxes(FreeCells, FreeLabels, MaxSpanOf, [](int32 A, int32 B) { return A == B; }, Boxes);

			TArray<int32> BoxOfDense;
			BoxOfDense.Init(INDEX_NONE, static_cast<int32>(Solid.Num()));
			auto ForBoxCells = [](const PCGVoxelChunking::FBox& Box, TFunctionRef<void(const FIntVector&)> Fn)
			{
				for (int32 Z = 0; Z < Box.Span.Z; ++Z)
					for (int32 Y = 0; Y < Box.Span.Y; ++Y)
						for (int32 X = 0; X < Box.Span.X; ++X)
						{
							Fn(Box.First + FIntVector(X, Y, Z));
						}
			};
			for (int32 B = 0; B < Boxes.Num(); ++B)
			{
				ForBoxCells(Boxes[B], [&](const FIntVector& Cell) { BoxOfDense[Solid.Index(Cell)] = B; });
			}
			// The layer of cells just outside one face of a box: Side 0 below First, 1 past First + Span.
			auto ForFaceLayer = [&](const PCGVoxelChunking::FBox& Box, int32 Axis, int32 Side, TFunctionRef<void(int32 Other)> Fn)
			{
				PCGVoxelChunking::FBox Layer = Box;
				Layer.First[Axis] = Side ? Box.First[Axis] + Box.Span[Axis] : Box.First[Axis] - 1;
				Layer.Span[Axis] = 1;
				ForBoxCells(Layer, [&](const FIntVector& Cell)
				{
					if (Solid.Contains(Cell) && BoxOfDense[Solid.Index(Cell)] != INDEX_NONE)
					{
						Fn(BoxOfDense[Solid.Index(Cell)]);
					}
				});
			};
			// A box of more than one cell touching no other box would be a cluster of one vertex, which PCGEx drops:
			// split it in two along its longest side so the pocket stays reachable as a pair.
			const int32 NumGrown = Boxes.Num();
			for (int32 B = 0; B < NumGrown; ++B)
			{
				const FIntVector Span = Boxes[B].Span;
				if (Span == FIntVector(1))
				{
					continue;
				}
				bool bTouches = false;
				for (int32 Face = 0; Face < 6 && !bTouches; ++Face)
				{
					ForFaceLayer(Boxes[B], Face / 2, Face % 2, [&](int32) { bTouches = true; });
				}
				if (bTouches)
				{
					continue;
				}
				const int32 Axis = Span.X >= Span.Y && Span.X >= Span.Z ? 0 : (Span.Y >= Span.Z ? 1 : 2);
				PCGVoxelChunking::FBox Upper = Boxes[B];
				Boxes[B].Span[Axis] = Span[Axis] / 2;
				Upper.First[Axis] += Boxes[B].Span[Axis];
				Upper.Span[Axis] = Span[Axis] - Boxes[B].Span[Axis];
				const int32 U = Boxes.Add(Upper);
				ForBoxCells(Upper, [&](const FIntVector& Cell) { BoxOfDense[Solid.Index(Cell)] = U; });
			}

			// Centre vertices: vertex B is box B.
			Verts.Reserve(Boxes.Num() * 2);
			for (const PCGVoxelChunking::FBox& Box : Boxes)
			{
				Verts.Add({ FVector(Box.First) + FVector(Box.Span - FIntVector(1)) * 0.5, FVector(Box.Span - FIntVector(1) + AgentCells) * 0.5,
					FVector(Box.Span), Box.Rep == 1 });
			}
			// Shared faces, each found once from the box below it: same face, centre to centre; otherwise portals.
			TArray<TArray<int32>> PortalsOf; // per box: its portal vertices
			PortalsOf.SetNum(Boxes.Num());
			TArray<int32> Neighbours;
			for (int32 B = 0; B < Boxes.Num(); ++B)
			{
				const PCGVoxelChunking::FBox& Box = Boxes[B];
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const int32 A1 = (Axis + 1) % 3;
					const int32 A2 = (Axis + 2) % 3;
					Neighbours.Reset();
					ForFaceLayer(Box, Axis, 1, [&](int32 Other) { Neighbours.AddUnique(Other); });
					for (const int32 N : Neighbours)
					{
						const PCGVoxelChunking::FBox& Other = Boxes[N];
						if (Other.First[A1] == Box.First[A1] && Other.First[A2] == Box.First[A2]
							&& Other.Span[A1] == Box.Span[A1] && Other.Span[A2] == Box.Span[A2])
						{
							EdgeList.Add(FIntVector2(B, N)); // the two make one box, so the line between centres stays inside
							continue;
						}
						const FIntVector TileMax(
							FMath::Min(MaxSpanOf(Box.Rep).X, MaxSpanOf(Other.Rep).X),
							FMath::Min(MaxSpanOf(Box.Rep).Y, MaxSpanOf(Other.Rep).Y),
							FMath::Min(MaxSpanOf(Box.Rep).Z, MaxSpanOf(Other.Rep).Z));
						int32 Lo[3] = { 0, 0, 0 }, Count[3] = { 0, 0, 0 }, Tiles[3] = { 1, 1, 1 };
						for (const int32 A : { A1, A2 })
						{
							Lo[A] = FMath::Max(Box.First[A], Other.First[A]);
							Count[A] = FMath::Min(Box.First[A] + Box.Span[A], Other.First[A] + Other.Span[A]) - Lo[A];
							Tiles[A] = FMath::DivideAndRoundUp(Count[A], FMath::Max(1, TileMax[A]));
						}
						for (int32 T2 = 0; T2 < Tiles[A2]; ++T2)
						{
							for (int32 T1 = 0; T1 < Tiles[A1]; ++T1)
							{
								FVector Pos, Half;
								Pos[Axis] = Box.First[Axis] + Box.Span[Axis] - 0.5;
								Half[Axis] = AgentCells[Axis] * 0.5;
								for (const int32 A : { A1, A2 })
								{
									const int32 K = A == A1 ? T1 : T2;
									const int32 TileLo = Lo[A] + (K * Count[A]) / Tiles[A];
									const int32 TileHi = Lo[A] + ((K + 1) * Count[A]) / Tiles[A] - 1;
									Pos[A] = (TileLo + TileHi) * 0.5;
									Half[A] = (TileHi - TileLo + AgentCells[A]) * 0.5;
								}
								const int32 P = Verts.Add({ Pos, Half, FVector::ZeroVector, Box.Rep == 1 && Other.Rep == 1, true });
								EdgeList.Add(FIntVector2(B, P));
								EdgeList.Add(FIntVector2(N, P));
								PortalsOf[B].Add(P);
								PortalsOf[N].Add(P);
							}
						}
					}
				}
			}
			// Hub cubes (designer's sketch, 2026-11-17): in each box two or more portals open into, nested cubes round the
			// centre, cube c of N at (c + 1) / (N + 1) of the way from the centre to the box's extent (one cube: halfway).
			// Each cube's surface is a lattice of Hub Subdivisions steps per side joined along its lines; the innermost
			// cube's corners join the centre, each outer cube's points join the same points of the cube inside it, and
			// each portal joins the outermost cube's nearest point (every point tied for nearest). Everything stays
			// inside the convex box, so every edge stays in free space.
			const int32 NumCubes = Settings->HubCubes;
			const int32 S = FMath::Max(1, Settings->HubSubdivisions);
			const int32 Side = S + 1;
			for (int32 B = 0; NumCubes > 0 && B < Boxes.Num(); ++B)
			{
				if (PortalsOf[B].Num() < 2)
				{
					continue; // a dead end or a corridor link: nothing to shape a path through
				}
				const PCGVoxelChunking::FBox& Box = Boxes[B];
				const FVector Centre = Verts[B].CellPos;
				const FVector Half = FVector(Box.Span - FIntVector(1)) * 0.5;
				const bool bAerial = Box.Rep == 1;
				TArray<int32> Inner; // lattice ids of the last cube built, INDEX_NONE where none
				bool bInnerCollapsed[3] = { false, false, false };
				for (int32 Cube = 0; Cube < NumCubes; ++Cube)
				{
					const double T = static_cast<double>(Cube + 1) / (NumCubes + 1);
					bool bCollapsed[3];
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						bCollapsed[Axis] = T * Half[Axis] < 0.25; // under a quarter cell: the cube is flat on this axis
					}
					if (bCollapsed[0] && bCollapsed[1] && bCollapsed[2])
					{
						continue; // no more than the centre
					}
					TArray<int32> Ids;
					Ids.Init(INDEX_NONE, Side * Side * Side);
					auto LatticeIndex = [Side](int32 I, int32 J, int32 K) { return I + Side * (J + Side * K); };
					for (int32 K = 0; K < Side; ++K)
						for (int32 J = 0; J < Side; ++J)
							for (int32 I = 0; I < Side; ++I)
							{
								const int32 Index[3] = { I, J, K };
								bool bValid = true, bSurface = false;
								FVector Pos;
								for (int32 Axis = 0; Axis < 3; ++Axis)
								{
									if (bCollapsed[Axis])
									{
										bValid &= Index[Axis] == 0;
										bSurface = true;
										Pos[Axis] = Centre[Axis];
									}
									else
									{
										bSurface |= Index[Axis] == 0 || Index[Axis] == S;
										Pos[Axis] = Centre[Axis] + T * Half[Axis] * (-1.0 + 2.0 * Index[Axis] / S);
									}
								}
								if (!bValid || !bSurface)
								{
									continue;
								}
								Ids[LatticeIndex(I, J, K)] = Pos.Equals(Centre, 1e-6) ? B
									: Verts.Add({ Pos, FVector(AgentCells) * 0.5, FVector::ZeroVector, bAerial, false });
							}
					// Lines of the lattice.
					for (int32 K = 0; K < Side; ++K)
						for (int32 J = 0; J < Side; ++J)
							for (int32 I = 0; I < Side; ++I)
							{
								const int32 From = Ids[LatticeIndex(I, J, K)];
								if (From == INDEX_NONE)
								{
									continue;
								}
								const int32 Steps[3][3] = { { I + 1, J, K }, { I, J + 1, K }, { I, J, K + 1 } };
								for (int32 Axis = 0; Axis < 3; ++Axis)
								{
									if (Steps[Axis][Axis] < Side)
									{
										const int32 To = Ids[LatticeIndex(Steps[Axis][0], Steps[Axis][1], Steps[Axis][2])];
										if (To != INDEX_NONE)
										{
											EdgeList.Add(FIntVector2(From, To));
										}
									}
								}
							}
					if (Inner.IsEmpty())
					{
						// Innermost cube: its corners join the centre.
						for (int32 Corner = 0; Corner < 8; ++Corner)
						{
							const int32 Index[3] = { (Corner & 1) ? S : 0, (Corner & 2) ? S : 0, (Corner & 4) ? S : 0 };
							int32 At[3];
							for (int32 Axis = 0; Axis < 3; ++Axis)
							{
								At[Axis] = bCollapsed[Axis] ? 0 : Index[Axis];
							}
							const int32 Id = Ids[LatticeIndex(At[0], At[1], At[2])];
							if (Id != INDEX_NONE)
							{
								EdgeList.Add(FIntVector2(B, Id));
							}
						}
					}
					else
					{
						// Each point joins the same point of the cube inside (flat axes of that cube read index 0).
						for (int32 K = 0; K < Side; ++K)
							for (int32 J = 0; J < Side; ++J)
								for (int32 I = 0; I < Side; ++I)
								{
									const int32 Id = Ids[LatticeIndex(I, J, K)];
									if (Id == INDEX_NONE)
									{
										continue;
									}
									const int32 Index[3] = { I, J, K };
									int32 At[3];
									for (int32 Axis = 0; Axis < 3; ++Axis)
									{
										At[Axis] = bInnerCollapsed[Axis] ? 0 : Index[Axis];
									}
									const int32 InnerId = Inner[LatticeIndex(At[0], At[1], At[2])];
									if (InnerId != INDEX_NONE)
									{
										EdgeList.Add(FIntVector2(Id, InnerId));
									}
								}
					}
					Inner = MoveTemp(Ids);
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						bInnerCollapsed[Axis] = bCollapsed[Axis];
					}
				}
				if (Inner.IsEmpty())
				{
					continue;
				}
				// Portals join the outermost cube's nearest points.
				for (const int32 Portal : PortalsOf[B])
				{
					const FVector& P = Verts[Portal].CellPos;
					double Best = TNumericLimits<double>::Max();
					for (const int32 Id : Inner)
					{
						if (Id != INDEX_NONE)
						{
							Best = FMath::Min(Best, FVector::DistSquared(P, Verts[Id].CellPos));
						}
					}
					for (const int32 Id : Inner)
					{
						if (Id != INDEX_NONE && FVector::DistSquared(P, Verts[Id].CellPos) <= Best + 1e-6)
						{
							EdgeList.Add(FIntVector2(Portal, Id));
						}
					}
				}
			}
		}
		// One edge per pair (hub lattices and centre links can meet the same pair twice), none from a vertex to itself.
		for (FIntVector2& Edge : EdgeList)
		{
			if (Edge.X > Edge.Y)
			{
				Swap(Edge.X, Edge.Y);
			}
		}
		EdgeList.RemoveAll([](const FIntVector2& Edge) { return Edge.X == Edge.Y; });
		EdgeList.Sort([](const FIntVector2& A, const FIntVector2& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		EdgeList.SetNum(Algo::Unique(EdgeList));
		if (EdgeList.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
				LOCTEXT("NoEdges", "no two vertices connect, so there is no cluster ({0} cells where the agent fits). Lower Agent Size or Surface Clearance, or give it more room."),
				FText::AsNumber(VtxDense.Num()))), Context);
			continue;
		}

		// ---- Islands; vertices with no edge are left out (PCGEx has no cluster of one) ----
		FUnionFind Islands(Verts.Num());
		TArray<int32> Degree;
		Degree.Init(0, Verts.Num());
		for (const FIntVector2& Edge : EdgeList)
		{
			Islands.Union(Edge.X, Edge.Y);
			++Degree[Edge.X];
			++Degree[Edge.Y];
		}
		TArray<int32> FinalOf;
		FinalOf.Init(INDEX_NONE, Verts.Num());
		TArray<int32> Kept;
		for (int32 V = 0; V < Verts.Num(); ++V)
		{
			if (Degree[V] > 0)
			{
				FinalOf[V] = Kept.Add(V);
			}
		}

		// ---- Vtx data ----
		const int32 NumVtx = Kept.Num();
		UPCGPointArrayData* VtxData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		VtxData->SetNumPoints(NumVtx, /*bInitializeValues=*/true);
		{
			TPCGValueRange<FTransform> Transforms = VtxData->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = VtxData->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = VtxData->GetBoundsMaxValueRange();
			TPCGValueRange<float> Densities = VtxData->GetDensityValueRange();
			TPCGValueRange<int32> Seeds = VtxData->GetSeedValueRange();
			TPCGValueRange<int64> Entries = VtxData->GetMetadataEntryValueRange();
			for (int32 K = 0; K < NumVtx; ++K)
			{
				const FNavVertex& Vert = Verts[Kept[K]];
				Transforms[K] = FTransform(Grid.GridToWorld.GetRotation(), VertexPosition(Vert.CellPos));
				Mins[K] = -Vert.HalfCells * Grid.VoxelSize;
				Maxs[K] = Vert.HalfCells * Grid.VoxelSize;
				Densities[K] = 1.0f;
				const FIntVector Scaled(FMath::RoundToInt32(Vert.CellPos.X * 64.0), FMath::RoundToInt32(Vert.CellPos.Y * 64.0), FMath::RoundToInt32(Vert.CellPos.Z * 64.0));
				Seeds[K] = static_cast<int32>(HashCombineFast(GetTypeHash(Scaled), 0x9E3779B9u));
				Entries[K] = VtxData->MutableMetadata()->AddEntry();
			}
		}
		{
			TArray<int64> VData;
			VData.SetNumUninitialized(NumVtx);
			for (int32 K = 0; K < NumVtx; ++K)
			{
				VData[K] = PCGUtilsClusterInterop::MakeVtxData(K, Degree[Kept[K]]);
			}
			PCGVoxelPointIO::WritePointAttribute<int64>(VtxData, PCGUtilsClusterInterop::VtxDataAttribute, 0, VData);
		}
		if (Settings->bOutputIsAerial)
		{
			TArray<bool> Values;
			Values.SetNumUninitialized(NumVtx);
			for (int32 K = 0; K < NumVtx; ++K)
			{
				Values[K] = Verts[Kept[K]].bAerial;
			}
			PCGVoxelPointIO::WritePointAttribute<bool>(VtxData, Settings->IsAerialAttribute, false, Values);
		}
		if (Settings->bOutputIsPortal)
		{
			TArray<bool> Values;
			Values.SetNumUninitialized(NumVtx);
			for (int32 K = 0; K < NumVtx; ++K)
			{
				Values[K] = Verts[Kept[K]].bPortal;
			}
			PCGVoxelPointIO::WritePointAttribute<bool>(VtxData, Settings->IsPortalAttribute, false, Values);
		}
		if (Settings->bOutputChunkSize)
		{
			TArray<FVector> Values;
			Values.SetNumUninitialized(NumVtx);
			for (int32 K = 0; K < NumVtx; ++K)
			{
				Values[K] = Verts[Kept[K]].ChunkSize;
			}
			PCGVoxelPointIO::WritePointAttribute<FVector>(VtxData, Settings->ChunkSizeAttribute, FVector::ZeroVector, Values);
		}
		if (Settings->bOutputSurfaceDistance || Settings->bOutputNearestVoxelIndex)
		{
			PCGVoxelMorphology::FNearestSet Nearest;
			PCGVoxelMorphology::NearestSet(Solid, Grid.VoxelSize, AxisPriority, Nearest);
			// Of the nearest filled cells of every cell the vertex touches (one inside a cell, two to eight on a boundary), the
			// one closest to the vertex itself; on a cell centre this is exactly NearestSet.
			TArray<double> Distances;
			TArray<int32> Indices;
			Distances.SetNumUninitialized(NumVtx);
			Indices.SetNumUninitialized(NumVtx);
			for (int32 K = 0; K < NumVtx; ++K)
			{
				const FVector& Pos = Verts[Kept[K]].CellPos;
				FIntVector Lo, Hi;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					Lo[Axis] = FMath::FloorToInt32(Pos[Axis] + 0.5 - 1e-6);
					Hi[Axis] = FMath::FloorToInt32(Pos[Axis] + 0.5 + 1e-6);
				}
				double Best = TNumericLimits<double>::Max();
				int64 BestSource = INDEX_NONE;
				for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
					for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
						for (int32 X = Lo.X; X <= Hi.X; ++X)
						{
							const FIntVector Touched(X, Y, Z);
							if (!Solid.Contains(Touched))
							{
								continue;
							}
							const int64 Source = Nearest.Source[Solid.Index(Touched)];
							if (Source == INDEX_NONE)
							{
								continue;
							}
							const FVector Gap = (Pos - FVector(Solid.Cell(Source))).GetAbs() - FVector(0.5);
							const double Distance = (FVector::Max(Gap, FVector::ZeroVector) * Grid.VoxelSize).Size();
							if (Distance < Best)
							{
								Best = Distance;
								BestSource = Source;
							}
						}
				Distances[K] = BestSource == INDEX_NONE ? -1.0 : Best;
				Indices[K] = BestSource == INDEX_NONE ? INDEX_NONE : PointOfDense[BestSource];
			}
			if (Settings->bOutputSurfaceDistance)
			{
				PCGVoxelPointIO::WritePointAttribute<double>(VtxData, Settings->SurfaceDistanceAttribute, -1.0, Distances);
			}
			if (Settings->bOutputNearestVoxelIndex)
			{
				PCGVoxelPointIO::WritePointAttribute<int32>(VtxData, Settings->NearestVoxelIndexAttribute, INDEX_NONE, Indices);
			}
		}

		const int64 PairId = static_cast<int64>(VtxData->GetUniqueID());
		FPCGTaggedData& VtxTagged = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		VtxTagged.Data = VtxData;
		VtxTagged.Pin = PCGVoxelBuildNavGridPins::Vtx;
		VtxTagged.Tags.Add(PCGUtilsClusterInterop::VtxTag);
		VtxTagged.Tags.Add(PCGUtilsClusterInterop::MakeClusterPairTag(PairId));

		// ---- Edges data, one per island, islands in the order of their first vertex ----
		TMap<int32, int32> IslandOfRoot;
		TArray<TArray<int32>> IslandEdges;
		for (int32 E = 0; E < EdgeList.Num(); ++E)
		{
			const int32 Root = Islands.Find(EdgeList[E].X);
			int32& Island = IslandOfRoot.FindOrAdd(Root, INDEX_NONE);
			if (Island == INDEX_NONE)
			{
				Island = IslandEdges.AddDefaulted();
			}
			IslandEdges[Island].Add(E);
		}
		IslandEdges.Sort([&](const TArray<int32>& A, const TArray<int32>& B)
		{
			return FMath::Min(FinalOf[EdgeList[A[0]].X], FinalOf[EdgeList[A[0]].Y]) < FMath::Min(FinalOf[EdgeList[B[0]].X], FinalOf[EdgeList[B[0]].Y]);
		});
		const TConstPCGValueRange<FTransform> VtxTransforms = VtxData->GetConstTransformValueRange();
		for (const TArray<int32>& Island : IslandEdges)
		{
			UPCGPointArrayData* EdgesData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
			EdgesData->SetNumPoints(Island.Num(), /*bInitializeValues=*/true);
			TPCGValueRange<FTransform> Transforms = EdgesData->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = EdgesData->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = EdgesData->GetBoundsMaxValueRange();
			TPCGValueRange<float> Densities = EdgesData->GetDensityValueRange();
			TPCGValueRange<int64> Entries = EdgesData->GetMetadataEntryValueRange();
			TArray<int64> EData;
			EData.SetNumUninitialized(Island.Num());
			for (int32 K = 0; K < Island.Num(); ++K)
			{
				const int32 A = FinalOf[EdgeList[Island[K]].X];
				const int32 B = FinalOf[EdgeList[Island[K]].Y];
				Transforms[K] = FTransform(Grid.GridToWorld.GetRotation(), (VtxTransforms[A].GetLocation() + VtxTransforms[B].GetLocation()) * 0.5);
				Mins[K] = FVector::ZeroVector;
				Maxs[K] = FVector::ZeroVector;
				Densities[K] = 1.0f;
				Entries[K] = EdgesData->MutableMetadata()->AddEntry();
				EData[K] = PCGUtilsClusterInterop::MakeEdgeData(A, B);
			}
			PCGVoxelPointIO::WritePointAttribute<int64>(EdgesData, PCGUtilsClusterInterop::EdgeDataAttribute, -1, EData);
			FPCGTaggedData& EdgesTagged = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			EdgesTagged.Data = EdgesData;
			EdgesTagged.Pin = PCGVoxelBuildNavGridPins::Edges;
			EdgesTagged.Tags.Add(PCGUtilsClusterInterop::EdgesTag);
			EdgesTagged.Tags.Add(PCGUtilsClusterInterop::MakeClusterPairTag(PairId));
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
