// Copyright Max Harris

#include "Elements/PCGVoxelRasterize.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelRasterize"

namespace PCGVoxelRasterizePrivate
{
	// Everything below works in cell space: grid space divided by Voxel Size, so cell (i, j, k) is the unit box
	// centred on (i, j, k).

	enum : uint8
	{
		BitFill = 1,
		BitIntersect = 2,
		BitSurround = 4,
	};

	/** A surface this close to a cell's face (in cells) is lying on it, not passing through it. */
	constexpr double OnFace = 1e-4;

	/** Inside rays are nudged off the cell centre lines by these odd amounts, so they never run exactly along an edge. */
	constexpr double RayNudgeA = 1.7317e-4;
	constexpr double RayNudgeB = 2.9123e-4;

	/** Strict separating-axis test of a triangle against the box of half size H centred on the origin. Touching is not overlap. */
	bool TriangleOverlapsBox(const FVector& H, const FVector& V0, const FVector& V1, const FVector& V2)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Min = FMath::Min3(V0[Axis], V1[Axis], V2[Axis]);
			const double Max = FMath::Max3(V0[Axis], V1[Axis], V2[Axis]);
			if (Min >= H[Axis] || Max <= -H[Axis])
			{
				return false;
			}
		}
		const FVector Normal = (V1 - V0) ^ (V2 - V1);
		{
			const double R = H.X * FMath::Abs(Normal.X) + H.Y * FMath::Abs(Normal.Y) + H.Z * FMath::Abs(Normal.Z);
			const double S = Normal | V0;
			if (S >= R || S <= -R)
			{
				return false;
			}
		}
		const FVector Edges[3] = { V1 - V0, V2 - V1, V0 - V2 };
		for (const FVector& Edge : Edges)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FVector Unit = FVector::ZeroVector;
				Unit[Axis] = 1.0;
				const FVector A = Unit ^ Edge;
				const double R = H.X * FMath::Abs(A.X) + H.Y * FMath::Abs(A.Y) + H.Z * FMath::Abs(A.Z);
				if (R <= UE_DOUBLE_SMALL_NUMBER)
				{
					continue;
				}
				const double P0 = A | V0;
				const double P1 = A | V1;
				const double P2 = A | V2;
				if (FMath::Min3(P0, P1, P2) >= R || FMath::Max3(P0, P1, P2) <= -R)
				{
					return false;
				}
			}
		}
		return true;
	}

	/** Strict test of a 2D segment against the square of half size H centred on the origin. */
	bool SegmentOverlapsSquare(const FVector2D& H, const FVector2D& A, const FVector2D& B)
	{
		if (FMath::Min(A.X, B.X) >= H.X || FMath::Max(A.X, B.X) <= -H.X
			|| FMath::Min(A.Y, B.Y) >= H.Y || FMath::Max(A.Y, B.Y) <= -H.Y)
		{
			return false;
		}
		const FVector2D Normal(A.Y - B.Y, B.X - A.X);
		const double R = H.X * FMath::Abs(Normal.X) + H.Y * FMath::Abs(Normal.Y);
		const double S = Normal | A;
		return S < R && S > -R;
	}

	/** Neighbour offsets for growing the Surround shell. */
	TArray<FIntVector> NeighborOffsets(EPCGVoxelRasterizeNeighbors Neighbors, bool b2D)
	{
		TArray<FIntVector> Offsets;
		const int32 ZRange = b2D ? 0 : 1;
		for (int32 Z = -ZRange; Z <= ZRange; ++Z)
			for (int32 Y = -1; Y <= 1; ++Y)
				for (int32 X = -1; X <= 1; ++X)
				{
					const int32 Steps = FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z);
					if (Steps == 0)
					{
						continue;
					}
					const int32 MaxSteps = Neighbors == EPCGVoxelRasterizeNeighbors::Faces ? 1
						: (Neighbors == EPCGVoxelRasterizeNeighbors::FacesAndEdges ? 2 : 3);
					if (Steps <= MaxSteps)
					{
						Offsets.Add(FIntVector(X, Y, Z));
					}
				}
		return Offsets;
	}

	/** A box of cells with one byte of mode bits per cell, X fastest. */
	struct FCellBox
	{
		FIntVector Min = FIntVector::ZeroValue;
		FIntVector Dims = FIntVector::ZeroValue;
		TArray<uint8> Bits;

		int32 Index(int32 X, int32 Y, int32 Z) const { return (X - Min.X) + Dims.X * ((Y - Min.Y) + Dims.Y * (Z - Min.Z)); }
		bool Contains(const FIntVector& C) const
		{
			return C.X >= Min.X && C.Y >= Min.Y && C.Z >= Min.Z && C.X < Min.X + Dims.X && C.Y < Min.Y + Dims.Y && C.Z < Min.Z + Dims.Z;
		}
		FIntVector Cell(int32 Index) const
		{
			const int32 X = Index % Dims.X;
			const int32 Rest = Index / Dims.X;
			return FIntVector(X, Rest % Dims.Y, Rest / Dims.Y) + Min;
		}
	};

	/** Grows Surround from every Fill or Intersect cell, Layers deep, through cells that are neither. */
	void GrowSurround(FCellBox& Box, const TArray<FIntVector>& Offsets, int32 Layers)
	{
		TArray<int32> Frontier;
		for (int32 Index = 0; Index < Box.Bits.Num(); ++Index)
		{
			if (Box.Bits[Index] & (BitFill | BitIntersect))
			{
				Frontier.Add(Index);
			}
		}
		TArray<int32> Next;
		for (int32 Layer = 0; Layer < Layers && !Frontier.IsEmpty(); ++Layer)
		{
			Next.Reset();
			for (const int32 Index : Frontier)
			{
				const FIntVector Cell = Box.Cell(Index);
				for (const FIntVector& Offset : Offsets)
				{
					const FIntVector N = Cell + Offset;
					if (!Box.Contains(N))
					{
						continue;
					}
					const int32 NIndex = Box.Index(N.X, N.Y, N.Z);
					if (Box.Bits[NIndex] == 0)
					{
						Box.Bits[NIndex] = BitSurround;
						Next.Add(NIndex);
					}
				}
			}
			Swap(Frontier, Next);
		}
	}

	/** Sets up Box over [Min, Max]; false (with the box size in OutDims) when it is over MaxCells. */
	bool InitBox(FCellBox& Box, const FIntVector& Min, const FIntVector& Max, int64 MaxCells, int64& OutCount)
	{
		Box.Min = Min;
		Box.Dims = Max - Min + FIntVector(1);
		OutCount = static_cast<int64>(Box.Dims.X) * Box.Dims.Y * Box.Dims.Z;
		if (OutCount > MaxCells || OutCount > (1ll << 30))
		{
			return false;
		}
		Box.Bits.SetNumZeroed(static_cast<int32>(OutCount));
		return true;
	}

	struct FHit
	{
		int32 Column;
		double At;
		int32 Sign;
	};

	/**
	 * Sorts the hits and sweeps each column: every run where the winding number is not 0 is passed to MarkInside.
	 * Returns how many columns end with a winding other than 0 (the surface is not closed there); their open run is skipped.
	 */
	int32 FillColumns(TArray<FHit>& Hits, TFunctionRef<void(int32 Column, double From, double To)> MarkInside)
	{
		Algo::Sort(Hits, [](const FHit& A, const FHit& B) { return A.Column != B.Column ? A.Column < B.Column : A.At < B.At; });
		TArray<int32> Starts;
		for (int32 Index = 0; Index < Hits.Num(); ++Index)
		{
			if (Index == 0 || Hits[Index].Column != Hits[Index - 1].Column)
			{
				Starts.Add(Index);
			}
		}
		Starts.Add(Hits.Num());
		std::atomic<int32> Unbalanced = 0;
		ParallelFor(Starts.Num() - 1, [&](int32 Run)
		{
			int32 Winding = 0;
			double Entered = 0.0;
			for (int32 Index = Starts[Run]; Index < Starts[Run + 1]; ++Index)
			{
				const int32 Before = Winding;
				Winding += Hits[Index].Sign;
				if (Before == 0 && Winding != 0)
				{
					Entered = Hits[Index].At;
				}
				else if (Before != 0 && Winding == 0)
				{
					MarkInside(Hits[Index].Column, Entered, Hits[Index].At);
				}
			}
			if (Winding != 0)
			{
				++Unbalanced;
			}
		});
		return Unbalanced.load();
	}

	/** Cells From < i < To (centres strictly between two crossings) along one column. */
	void CellRange(double From, double To, int32& OutFirst, int32& OutLast)
	{
		OutFirst = FMath::FloorToInt32(From) + 1;
		OutLast = FMath::CeilToInt32(To) - 1;
	}
}

#if WITH_EDITOR
FText UPCGVoxelRasterizeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Rasterize");
}

FText UPCGVoxelRasterizeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Turns each DynMesh (Source Mesh) or closed point path (Source Path, filled in the grid's XY plane) into a voxel set. "
		"Modes: Fill (cells whose centre is inside), Intersect (cells the surface passes through) and Surround (the shell just "
		"outside), any mix. Connect a voxel set to Grid to put the cells on its grid, so they line up with it for Voxel | Boolean.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelRasterizeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point | EPCGDataType::DynamicMesh, true, true,
		LOCTEXT("InTooltip", "The shapes: DynMesh data when Source is Mesh, closed point paths when Source is Path. One voxel set per input."))
		.SetRequiredPin();
	Pins.Emplace(PCGVoxelRasterizePins::Grid, EPCGDataType::Point, true, true,
		LOCTEXT("GridTooltip", "Optional voxel set whose grid (Voxel Size and grid transform, read through Grid settings) the cells go on: "
			"one set for every input, or one per input. Its cells are not used. Without it, New Grid settings place the grid."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelRasterizeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One voxel set per input (plain cells, Voxel Size and grid transform in @Data), carrying the input's tags."));
	return Pins;
}

FPCGElementPtr UPCGVoxelRasterizeSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelRasterizeElement>();
}

bool FPCGVoxelRasterizeElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelRasterizePrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelRasterizeElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelRasterizeSettings* Settings = Context->GetInputSettings<UPCGVoxelRasterizeSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Rasterize");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};
	auto Range = [&](const TCHAR* Name, double Value, const FText& Expected)
	{
		Fail(FText::Format(LOCTEXT("BadSetting", "{0} is {1}; it must be {2}."), FText::FromString(Name), FText::AsNumber(Value), Expected));
	};

	// ---- Settings, each checked on its own ----
	const uint8 Modes = Settings->Modes & (BitFill | BitIntersect | BitSurround);
	if (Modes == 0)
	{
		Fail(LOCTEXT("NoModes", "Modes has none of Fill, Intersect and Surround turned on; turn on at least one."));
		return true;
	}
	if (!(Settings->TouchMargin > -0.5 && Settings->TouchMargin <= 1.0))
	{
		Range(TEXT("Touch Margin"), Settings->TouchMargin, LOCTEXT("RMargin", "above -0.5 and at most 1"));
		return true;
	}
	if (Settings->SurroundCells < 1)
	{
		Range(TEXT("Surround Cells"), Settings->SurroundCells, LOCTEXT("R1", "1 or greater"));
		return true;
	}
	if (Settings->CellsBelow < 0)
	{
		Range(TEXT("Cells Below"), Settings->CellsBelow, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->CellsAbove < 0)
	{
		Range(TEXT("Cells Above"), Settings->CellsAbove, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->MaxCells < 1)
	{
		Range(TEXT("Max Cells"), static_cast<double>(Settings->MaxCells), LOCTEXT("R1", "1 or greater"));
		return true;
	}
	if (Settings->bWriteModeAttribute && Settings->ModeAttribute.IsNone())
	{
		Fail(LOCTEXT("NoModeName", "Mode Attribute is empty while writing it is on; give it a name or turn it off."));
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	const TArray<FPCGTaggedData> GridInputs = Context->InputData.GetInputsByPin(PCGVoxelRasterizePins::Grid);
	if (GridInputs.Num() > 1 && GridInputs.Num() != Inputs.Num())
	{
		Fail(FText::Format(LOCTEXT("GridCount", "Grid has {0} inputs; it takes 1 (shared by every input) or one per input ({1})."),
			GridInputs.Num(), Inputs.Num()));
		return true;
	}

	const bool bWarnOpen = (Modes & (BitFill | BitSurround)) != 0;
	const bool b2D = Settings->Source == EPCGVoxelRasterizeSource::Path;
	const TArray<FIntVector> Offsets = NeighborOffsets(Settings->SurroundNeighbors, b2D);
	const int32 Pad = ((Modes & BitSurround) ? Settings->SurroundCells : 0) + 2;
	const double Half = 0.5 + Settings->TouchMargin - OnFace;

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		// ---- Grid frame ----
		FVector VoxelSize = Settings->VoxelSize;
		FTransform GridToWorld(Settings->GridTransform.GetRotation(), Settings->GridTransform.GetLocation());
		if (!GridInputs.IsEmpty())
		{
			const int32 GridIndex = GridInputs.Num() == 1 ? 0 : InputIndex;
			const UPCGBasePointData* GridPoints = Cast<const UPCGBasePointData>(GridInputs[GridIndex].Data);
			if (!GridPoints)
			{
				Fail(FText::Format(LOCTEXT("GridNotPoints", "Grid input {0} is not point data; it must be a voxel set."), GridIndex));
				continue;
			}
			if (!FPCGVoxelGrid::ReadFrame(Context, GridPoints, Settings->Grid, Title, GridIndex, VoxelSize, GridToWorld))
			{
				continue;
			}
		}
		else
		{
			bool bSizeOk = true;
			for (int32 Axis = 0; Axis < 3 && bSizeOk; ++Axis)
			{
				if (!(VoxelSize[Axis] > 0.0))
				{
					Fail(FText::Format(LOCTEXT("VoxelSizeAxis", "Voxel Size {0} is {1}; every axis must be greater than 0."),
						FText::FromString(Axis == 0 ? TEXT("X") : (Axis == 1 ? TEXT("Y") : TEXT("Z"))), FText::AsNumber(VoxelSize[Axis])));
					bSizeOk = false;
				}
			}
			if (!bSizeOk)
			{
				return true;
			}
		}
		auto ToCell = [&](const FVector& World) { return GridToWorld.InverseTransformPositionNoScale(World) / VoxelSize; };

		FCellBox Box;
		int64 BoxCount = 0;
		auto TooBig = [&]()
		{
			Fail(FText::Format(LOCTEXT("TooBig", "the box of cells to scan is {0} x {1} x {2} = {3} cells, over Max Cells ({4}) "
				"(or over 1,073,741,824). Use larger cells or raise Max Cells."),
				Box.Dims.X, Box.Dims.Y, Box.Dims.Z, BoxCount, Settings->MaxCells), InputIndex);
		};

		if (!b2D)
		{
			// ---------------- Mesh ----------------
			const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Inputs[InputIndex].Data);
			if (!MeshData)
			{
				Fail(FText::Format(LOCTEXT("NotMesh", "the input is {0}; Source is Mesh, which reads DynMesh data."),
					FText::FromString(Inputs[InputIndex].Data ? Inputs[InputIndex].Data->GetClass()->GetName() : TEXT("empty"))), InputIndex);
				continue;
			}
			if (!MeshData->GetDynamicMesh())
			{
				Fail(LOCTEXT("NoMesh", "the DynMesh data holds no mesh."), InputIndex);
				continue;
			}
			const UE::Geometry::FDynamicMesh3& Mesh = MeshData->GetDynamicMesh()->GetMeshRef();
			const FTransform MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, MeshData, Settings->bMeshInLocalSpace);

			TArray<FVector> Verts;
			Verts.SetNumZeroed(Mesh.MaxVertexID());
			FVector Lo(UE_BIG_NUMBER), Hi(-UE_BIG_NUMBER);
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				const FVector P = ToCell(MeshToWorld.TransformPosition(Mesh.GetVertex(VertexID)));
				Verts[VertexID] = P;
				Lo = Lo.ComponentMin(P);
				Hi = Hi.ComponentMax(P);
			}
			TArray<UE::Geometry::FIndex3i> Tris;
			Tris.Reserve(Mesh.TriangleCount());
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				Tris.Add(Mesh.GetTriangle(TriangleID));
			}

			if (!Tris.IsEmpty())
			{
				const FIntVector Min(FMath::FloorToInt32(Lo.X) - Pad, FMath::FloorToInt32(Lo.Y) - Pad, FMath::FloorToInt32(Lo.Z) - Pad);
				const FIntVector Max(FMath::CeilToInt32(Hi.X) + Pad, FMath::CeilToInt32(Hi.Y) + Pad, FMath::CeilToInt32(Hi.Z) + Pad);
				if (!InitBox(Box, Min, Max, Settings->MaxCells, BoxCount))
				{
					TooBig();
					continue;
				}

				// Intersect: every cell a triangle passes through.
				// Intersect and Fill are always worked out, so the mode bits are complete.
				{
					const FVector H(Half);
					ParallelFor(Tris.Num(), [&](int32 T)
					{
						const FVector& A = Verts[Tris[T].A];
						const FVector& B = Verts[Tris[T].B];
						const FVector& C = Verts[Tris[T].C];
						if (((B - A) ^ (C - A)).SizeSquared() <= UE_DOUBLE_SMALL_NUMBER * UE_DOUBLE_SMALL_NUMBER)
						{
							return;
						}
						const FVector TLo = A.ComponentMin(B).ComponentMin(C);
						const FVector THi = A.ComponentMax(B).ComponentMax(C);
						const FIntVector From(FMath::FloorToInt32(TLo.X - Half), FMath::FloorToInt32(TLo.Y - Half), FMath::FloorToInt32(TLo.Z - Half));
						const FIntVector To(FMath::CeilToInt32(THi.X + Half), FMath::CeilToInt32(THi.Y + Half), FMath::CeilToInt32(THi.Z + Half));
						for (int32 Z = FMath::Max(From.Z, Box.Min.Z); Z <= FMath::Min(To.Z, Box.Min.Z + Box.Dims.Z - 1); ++Z)
							for (int32 Y = FMath::Max(From.Y, Box.Min.Y); Y <= FMath::Min(To.Y, Box.Min.Y + Box.Dims.Y - 1); ++Y)
								for (int32 X = FMath::Max(From.X, Box.Min.X); X <= FMath::Min(To.X, Box.Min.X + Box.Dims.X - 1); ++X)
								{
									const FVector Center(X, Y, Z);
									if (TriangleOverlapsBox(H, A - Center, B - Center, C - Center))
									{
										FPlatformAtomics::InterlockedOr(reinterpret_cast<volatile int8*>(&Box.Bits[Box.Index(X, Y, Z)]), static_cast<int8>(BitIntersect));
									}
								}
					});
				}

				// Fill: rays along X through each (Y, Z) row of cell centres; nonzero winding is inside.
				{
					TArray<FHit> Hits;
					for (const UE::Geometry::FIndex3i& Tri : Tris)
					{
						const FVector& A = Verts[Tri.A];
						const FVector& B = Verts[Tri.B];
						const FVector& C = Verts[Tri.C];
						const double Area = (B.Y - A.Y) * (C.Z - A.Z) - (B.Z - A.Z) * (C.Y - A.Y);
						if (FMath::Abs(Area) <= UE_DOUBLE_SMALL_NUMBER)
						{
							continue;
						}
						const int32 Y0 = FMath::Max(FMath::CeilToInt32(FMath::Min3(A.Y, B.Y, C.Y) - RayNudgeA), Box.Min.Y);
						const int32 Y1 = FMath::Min(FMath::FloorToInt32(FMath::Max3(A.Y, B.Y, C.Y) - RayNudgeA), Box.Min.Y + Box.Dims.Y - 1);
						const int32 Z0 = FMath::Max(FMath::CeilToInt32(FMath::Min3(A.Z, B.Z, C.Z) - RayNudgeB), Box.Min.Z);
						const int32 Z1 = FMath::Min(FMath::FloorToInt32(FMath::Max3(A.Z, B.Z, C.Z) - RayNudgeB), Box.Min.Z + Box.Dims.Z - 1);
						for (int32 Z = Z0; Z <= Z1; ++Z)
						{
							for (int32 Y = Y0; Y <= Y1; ++Y)
							{
								const double PY = Y + RayNudgeA;
								const double PZ = Z + RayNudgeB;
								const double WA = ((B.Y - PY) * (C.Z - PZ) - (B.Z - PZ) * (C.Y - PY)) / Area;
								const double WB = ((C.Y - PY) * (A.Z - PZ) - (C.Z - PZ) * (A.Y - PY)) / Area;
								const double WC = 1.0 - WA - WB;
								if (WA < 0.0 || WB < 0.0 || WC < 0.0)
								{
									continue;
								}
								const int32 Column = (Y - Box.Min.Y) + Box.Dims.Y * (Z - Box.Min.Z);
								Hits.Add({ Column, WA * A.X + WB * B.X + WC * C.X, Area > 0.0 ? -1 : 1 });
							}
						}
					}
					const int32 Unbalanced = FillColumns(Hits, [&](int32 Column, double From, double To)
					{
						const int32 Y = Column % Box.Dims.Y + Box.Min.Y;
						const int32 Z = Column / Box.Dims.Y + Box.Min.Z;
						int32 First, Last;
						CellRange(From, To, First, Last);
						for (int32 X = FMath::Max(First, Box.Min.X); X <= FMath::Min(Last, Box.Min.X + Box.Dims.X - 1); ++X)
						{
							Box.Bits[Box.Index(X, Y, Z)] |= BitFill;
						}
					});
					if (Unbalanced > 0 && bWarnOpen)
					{
						PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
							LOCTEXT("OpenMesh", "{0} rows of cells cross the mesh's faces in more than out (the mesh is not closed), "
								"so Fill and Surround skip the rest of those rows. Close the mesh, or use Intersect only."), Unbalanced)), Context);
					}
				}
			}
		}
		else
		{
			// ---------------- Path ----------------
			const UPCGBasePointData* PathPoints = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
			if (!PathPoints)
			{
				Fail(FText::Format(LOCTEXT("NotPath", "the input is {0}; Source is Path, which reads point data."),
					FText::FromString(Inputs[InputIndex].Data ? Inputs[InputIndex].Data->GetClass()->GetName() : TEXT("empty"))), InputIndex);
				continue;
			}
			const TConstPCGValueRange<FTransform> Transforms = PathPoints->GetConstTransformValueRange();
			TArray<FVector> Loop;
			for (int32 Index = 0; Index < Transforms.Num(); ++Index)
			{
				const FVector P = ToCell(Transforms[Index].GetLocation());
				if (Loop.IsEmpty() || FVector2D(P - Loop.Last()).SizeSquared() > UE_KINDA_SMALL_NUMBER * UE_KINDA_SMALL_NUMBER)
				{
					Loop.Add(P);
				}
			}
			if (Loop.Num() > 1 && FVector2D(Loop[0] - Loop.Last()).SizeSquared() <= UE_KINDA_SMALL_NUMBER * UE_KINDA_SMALL_NUMBER)
			{
				Loop.Pop();
			}
			if (Loop.Num() < 3)
			{
				Fail(FText::Format(LOCTEXT("ShortPath", "the path has {0} distinct points in the grid's XY plane; a closed loop needs at least 3."),
					Loop.Num()), InputIndex);
				continue;
			}

			FVector Lo(UE_BIG_NUMBER), Hi(-UE_BIG_NUMBER);
			for (const FVector& P : Loop)
			{
				Lo = Lo.ComponentMin(P);
				Hi = Hi.ComponentMax(P);
			}
			const int32 ZLow = FMath::RoundToInt32(Lo.Z) - Settings->CellsBelow;
			const int32 ZHigh = FMath::RoundToInt32(Hi.Z) + Settings->CellsAbove;
			const FIntVector Min(FMath::FloorToInt32(Lo.X) - Pad, FMath::FloorToInt32(Lo.Y) - Pad, 0);
			const FIntVector Max(FMath::CeilToInt32(Hi.X) + Pad, FMath::CeilToInt32(Hi.Y) + Pad, 0);
			// The flat mask first; layers are copied at the end.
			if (!InitBox(Box, Min, Max, Settings->MaxCells, BoxCount))
			{
				TooBig();
				continue;
			}
			const int64 Layers = static_cast<int64>(ZHigh - ZLow + 1);
			if (BoxCount * Layers > Settings->MaxCells)
			{
				Box.Dims.Z = static_cast<int32>(Layers);
				BoxCount *= Layers;
				TooBig();
				continue;
			}

			const int32 N = Loop.Num();
			// Intersect and Fill are always worked out, so the mode bits are complete.
			{
				const FVector2D H(Half);
				for (int32 Edge = 0; Edge < N; ++Edge)
				{
					const FVector2D A(Loop[Edge]);
					const FVector2D B(Loop[(Edge + 1) % N]);
					const int32 X0 = FMath::Max(FMath::FloorToInt32(FMath::Min(A.X, B.X) - Half), Box.Min.X);
					const int32 X1 = FMath::Min(FMath::CeilToInt32(FMath::Max(A.X, B.X) + Half), Box.Min.X + Box.Dims.X - 1);
					const int32 Y0 = FMath::Max(FMath::FloorToInt32(FMath::Min(A.Y, B.Y) - Half), Box.Min.Y);
					const int32 Y1 = FMath::Min(FMath::CeilToInt32(FMath::Max(A.Y, B.Y) + Half), Box.Min.Y + Box.Dims.Y - 1);
					for (int32 Y = Y0; Y <= Y1; ++Y)
					{
						for (int32 X = X0; X <= X1; ++X)
						{
							const FVector2D Center(X, Y);
							if (SegmentOverlapsSquare(H, A - Center, B - Center))
							{
								Box.Bits[Box.Index(X, Y, 0)] |= BitIntersect;
							}
						}
					}
				}
			}
			{
				TArray<FHit> Hits;
				for (int32 Edge = 0; Edge < N; ++Edge)
				{
					const FVector& A = Loop[Edge];
					const FVector& B = Loop[(Edge + 1) % N];
					const int32 Y0 = FMath::Max(FMath::CeilToInt32(FMath::Min(A.Y, B.Y) - RayNudgeA), Box.Min.Y);
					const int32 Y1 = FMath::Min(FMath::FloorToInt32(FMath::Max(A.Y, B.Y) - RayNudgeA), Box.Min.Y + Box.Dims.Y - 1);
					for (int32 Y = Y0; Y <= Y1; ++Y)
					{
						const double PY = Y + RayNudgeA;
						if ((A.Y <= PY) == (B.Y <= PY))
						{
							continue;
						}
						const double X = A.X + (PY - A.Y) * (B.X - A.X) / (B.Y - A.Y);
						Hits.Add({ Y - Box.Min.Y, X, B.Y > A.Y ? 1 : -1 });
					}
				}
				FillColumns(Hits, [&](int32 Row, double From, double To)
				{
					int32 First, Last;
					CellRange(From, To, First, Last);
					for (int32 X = FMath::Max(First, Box.Min.X); X <= FMath::Min(Last, Box.Min.X + Box.Dims.X - 1); ++X)
					{
						Box.Bits[Box.Index(X, Row + Box.Min.Y, 0)] |= BitFill;
					}
				});
			}
			if (Modes & BitSurround)
			{
				GrowSurround(Box, Offsets, Settings->SurroundCells);
			}

			// Stack the flat mask into its layers.
			FCellBox Stacked;
			Stacked.Min = FIntVector(Box.Min.X, Box.Min.Y, ZLow);
			Stacked.Dims = FIntVector(Box.Dims.X, Box.Dims.Y, static_cast<int32>(Layers));
			Stacked.Bits.SetNumUninitialized(Box.Bits.Num() * static_cast<int32>(Layers));
			for (int32 Layer = 0; Layer < Layers; ++Layer)
			{
				FMemory::Memcpy(Stacked.Bits.GetData() + static_cast<int64>(Layer) * Box.Bits.Num(), Box.Bits.GetData(), Box.Bits.Num());
			}
			Box = MoveTemp(Stacked);
		}

		if (!b2D && (Modes & BitSurround) && !Box.Bits.IsEmpty())
		{
			GrowSurround(Box, Offsets, Settings->SurroundCells);
		}

		// ---- Output: the cells in a selected mode, ordered Z, Y, X ----
		TArray<int32> Emit;
		for (int32 Index = 0; Index < Box.Bits.Num(); ++Index)
		{
			if (Box.Bits[Index] & Modes)
			{
				Emit.Add(Index);
			}
		}
		const int32 NumOut = Emit.Num();
		UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
		Out->SetNumPoints(NumOut);
		Out->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin
			| EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);
		const FQuat GridRotation = GridToWorld.GetRotation();
		TPCGValueRange<FTransform> OutTransforms = Out->GetTransformValueRange();
		TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
		TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
		TPCGValueRange<int32> Seeds = Out->GetSeedValueRange();
		TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
		ParallelFor(NumOut, [&](int32 P)
		{
			const FIntVector Cell = Box.Cell(Emit[P]);
			OutTransforms[P] = FTransform(GridRotation, GridToWorld.TransformPositionNoScale(FVector(Cell) * VoxelSize));
			Mins[P] = -VoxelSize * 0.5;
			Maxs[P] = VoxelSize * 0.5;
			Seeds[P] = PCGHelpers::ComputeSeed(Cell.X, Cell.Y, Cell.Z);
			Entries[P] = PCGInvalidEntryKey;
		});
		PCGVoxelPointIO::MakeEntriesUnique(Out);
		if (Settings->bWriteModeAttribute)
		{
			TArray<int32> Values;
			Values.SetNumUninitialized(NumOut);
			for (int32 P = 0; P < NumOut; ++P)
			{
				Values[P] = Box.Bits[Emit[P]];
			}
			PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->ModeAttribute, 0, Values);
		}
		if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Out))
		{
			const FName SizeName = Settings->Grid.VoxelSize.Attribute.GetAttributeName();
			PCGUtilsDataDomain::WriteValue(*Domain, SizeName.IsNone() ? FName(TEXT("VoxelSize")) : SizeName, VoxelSize);
			PCGUtilsDataDomain::WriteValue(*Domain, Settings->Grid.GridTransformAttribute, GridToWorld);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
