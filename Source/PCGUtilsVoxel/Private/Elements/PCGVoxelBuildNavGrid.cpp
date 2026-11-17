// Copyright Max Harris

#include "Elements/PCGVoxelBuildNavGrid.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGUtilsClusterInterop.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
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
		const FIntVector Box = DomainHi - DomainLo + FIntVector(1 + 2 * PadCells);
		const int64 BoxCells = static_cast<int64>(Box.X) * Box.Y * Box.Z;
		if (BoxCells > Settings->MaxCells || BoxCells >= MAX_int32)
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to work over is {0} x {1} x {2} = {3} cells (the set and the space to fly in, "
				"plus room for clearance, the agent and gaps), over Max Cells ({4}, and under 2^31 at most). Raise Max Cells, shrink the Bounds or Padding, or split the set."),
				Box.X, Box.Y, Box.Z, FText::AsNumber(BoxCells), FText::AsNumber(Settings->MaxCells)), InputIndex);
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

		auto VertexPosition = [&](const FIntVector& Cell)
		{
			return Grid.GridToWorld.TransformPositionNoScale((FVector(Cell) + AgentOffset) * Grid.VoxelSize);
		};

		// ---- Vertices ----
		TArray<int32> VtxOfDense;
		VtxOfDense.Init(INDEX_NONE, static_cast<int32>(Solid.Num()));
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
					if (Bounds && !(Bounds->GetDensityAtPosition(VertexPosition(Cell)) > 0.0f))
					{
						continue;
					}
					VtxOfDense[I] = VtxDense.Add(I);
				}

		// ---- Edges: each neighbour once (the positive half of the offsets), no corner cut on a diagonal ----
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
		TArray<FIntVector2> EdgeList;
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
		if (EdgeList.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
				LOCTEXT("NoEdges", "no two vertices connect, so there is no cluster ({0} cells where the agent fits). Lower Agent Size or Surface Clearance, or give it more room."),
				FText::AsNumber(VtxDense.Num()))), Context);
			continue;
		}

		// ---- Islands; vertices with no edge are left out (PCGEx has no cluster of one) ----
		FUnionFind Islands(VtxDense.Num());
		TArray<int32> Degree;
		Degree.Init(0, VtxDense.Num());
		for (const FIntVector2& Edge : EdgeList)
		{
			Islands.Union(Edge.X, Edge.Y);
			++Degree[Edge.X];
			++Degree[Edge.Y];
		}
		TArray<int32> FinalOf;
		FinalOf.Init(INDEX_NONE, VtxDense.Num());
		TArray<int32> Kept;
		for (int32 V = 0; V < VtxDense.Num(); ++V)
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
			const FVector HalfAgent = FVector(AgentCells) * Grid.VoxelSize * 0.5;
			for (int32 K = 0; K < NumVtx; ++K)
			{
				const FIntVector Cell = Solid.Cell(VtxDense[Kept[K]]);
				Transforms[K] = FTransform(Grid.GridToWorld.GetRotation(), VertexPosition(Cell));
				Mins[K] = -HalfAgent;
				Maxs[K] = HalfAgent;
				Densities[K] = 1.0f;
				Seeds[K] = static_cast<int32>(HashCombineFast(GetTypeHash(Cell), 0x9E3779B9u));
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
		if (Settings->bOutputSurfaceDistance || Settings->bOutputNearestVoxelIndex)
		{
			PCGVoxelMorphology::FNearestSet Nearest;
			PCGVoxelMorphology::NearestSet(Solid, Grid.VoxelSize, AxisPriority, Nearest);
			if (Settings->bOutputSurfaceDistance)
			{
				TArray<double> Values;
				Values.SetNumUninitialized(NumVtx);
				for (int32 K = 0; K < NumVtx; ++K)
				{
					const double D = Nearest.DistanceSquared[VtxDense[Kept[K]]];
					Values[K] = Nearest.Source[VtxDense[Kept[K]]] == INDEX_NONE ? -1.0 : FMath::Sqrt(D);
				}
				PCGVoxelPointIO::WritePointAttribute<double>(VtxData, Settings->SurfaceDistanceAttribute, -1.0, Values);
			}
			if (Settings->bOutputNearestVoxelIndex)
			{
				TArray<int32> Values;
				Values.SetNumUninitialized(NumVtx);
				for (int32 K = 0; K < NumVtx; ++K)
				{
					const int64 Source = Nearest.Source[VtxDense[Kept[K]]];
					Values[K] = Source == INDEX_NONE ? INDEX_NONE : PointOfDense[Source];
				}
				PCGVoxelPointIO::WritePointAttribute<int32>(VtxData, Settings->NearestVoxelIndexAttribute, INDEX_NONE, Values);
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
