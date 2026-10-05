// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshLoft.h"

#include "Algo/Reverse.h"
#include "CompGeom/Delaunay2.h"
#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

namespace PCGUtilsDynMeshLoft
{
	namespace LoftPrivate
	{
		FVector3d SafeUpAxis(const FVector3d& UpAxis)
		{
			const FVector3d Normalized = UpAxis.GetSafeNormal();
			return Normalized.IsNearlyZero() ? FVector3d::UnitZ() : Normalized;
		}

		FVector3d Flatten(const FVector3d& Vector, const FVector3d& Up)
		{
			return Vector - Up * FVector3d::DotProduct(Vector, Up);
		}

		/** Direction of the rail segment starting at Index, flattened; zero for a degenerate segment. */
		FVector3d FlatSegmentDirection(TConstArrayView<FVector3d> Rail, int32 Index, const FVector3d& Up)
		{
			const int32 Next = (Index + 1) % Rail.Num();
			return Flatten(Rail[Next] - Rail[Index], Up).GetSafeNormal();
		}

		/** Twice the signed area the rail encloses about Up. */
		double SignedTwiceArea(TConstArrayView<FVector3d> Rail, const FVector3d& Up)
		{
			double TwiceArea = 0.0;
			for (int32 Index = 1; Index + 1 < Rail.Num(); ++Index)
			{
				TwiceArea += FVector3d::DotProduct(
					FVector3d::CrossProduct(Rail[Index] - Rail[0], Rail[Index + 1] - Rail[0]), Up);
			}
			return TwiceArea;
		}
	}

	bool BuildLoft(
		TConstArrayView<FVector3d> RailA,
		TConstArrayView<FVector3d> RailB,
		const FLoftOptions& Options,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FLoftResult& OutResult,
		FString& OutError)
	{
		using namespace UE::Geometry;

		const int32 NumColumns = RailA.Num();
		const int32 MinColumns = Options.bClosed ? 3 : 2;
		if (NumColumns < MinColumns)
		{
			OutError = FString::Printf(TEXT("Rail A has %d points; %s loft needs at least %d."),
				NumColumns, Options.bClosed ? TEXT("a closed") : TEXT("an open"), MinColumns);
			return false;
		}
		if (RailB.Num() != NumColumns)
		{
			OutError = FString::Printf(TEXT("Rail B has %d points but Rail A has %d; the rails must match one for one."),
				RailB.Num(), NumColumns);
			return false;
		}
		if (Options.NumRows < 1)
		{
			OutError = FString::Printf(TEXT("Rows is %d; it must be at least 1."), Options.NumRows);
			return false;
		}

		const FVector3d Up = LoftPrivate::SafeUpAxis(Options.UpAxis);
		const int32 NumRows = Options.NumRows;
		const int32 NumVertexRows = NumRows + 1;

		// Positions first, so the facing can be decided before any triangle is appended.
		TArray<FVector3d> Positions;
		Positions.SetNumUninitialized(NumColumns * NumVertexRows);
		const auto PositionAt = [&Positions, NumColumns](int32 Column, int32 Row) -> FVector3d&
		{
			return Positions[Row * NumColumns + Column];
		};

		for (int32 Row = 0; Row < NumVertexRows; ++Row)
		{
			const double T = static_cast<double>(Row) / NumRows;
			double HeightT = T;
			if (Row > 0 && Row < NumRows && Options.HeightProfile)
			{
				HeightT = Options.HeightProfile(T);
			}

			for (int32 Column = 0; Column < NumColumns; ++Column)
			{
				const FVector3d& A = RailA[Column];
				const FVector3d& B = RailB[Column];
				if (Row == 0)
				{
					PositionAt(Column, Row) = A;
				}
				else if (Row == NumRows)
				{
					PositionAt(Column, Row) = B;
				}
				else
				{
					const FVector3d Delta = B - A;
					const double HeightDelta = FVector3d::DotProduct(Delta, Up);
					PositionAt(Column, Row) = A + (Delta - Up * HeightDelta) * T + Up * (HeightDelta * HeightT);
				}
			}
		}

		const int32 NumQuadColumns = Options.bClosed ? NumColumns : NumColumns - 1;

		double Facing = 0.0;
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			for (int32 Column = 0; Column < NumQuadColumns; ++Column)
			{
				const int32 NextColumn = (Column + 1) % NumColumns;
				const FVector3d Along = PositionAt(NextColumn, Row) - PositionAt(Column, Row);
				const FVector3d Across = PositionAt(Column, Row + 1) - PositionAt(Column, Row);
				Facing += FVector3d::DotProduct(FVector3d::CrossProduct(Along, Across), Up);
			}
		}
		// FDynamicMesh3 takes a triangle's normal as (C - A) x (B - A), so the (column, next column, next row)
		// winding faces against Along x Across and has to be reversed when that cross product already points up.
		const bool bReverseWinding = (Facing > 0.0) != Options.bFlipFaces;

		if (!OutMesh.HasTriangleGroups())
		{
			OutMesh.EnableTriangleGroups();
		}
		const int32 GroupID = OutMesh.AllocateTriangleGroup();

		TArray<int32> VertexIDs;
		VertexIDs.SetNumUninitialized(Positions.Num());
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			VertexIDs[Index] = OutMesh.AppendVertex(Positions[Index]);
		}

		const auto OutResultRailB = [&VertexIDs, NumRows, NumColumns](int32 Column)
		{
			return VertexIDs[NumRows * NumColumns + Column];
		};

		if (!OutMesh.HasAttributes())
		{
			OutMesh.EnableAttributes();
		}
		FDynamicMeshUVOverlay* UVs = OutMesh.Attributes()->PrimaryUV();

		// A closed loft needs one extra UV column so U does not wrap backwards across the last quad.
		TArray<double> ColumnU;
		ColumnU.SetNumUninitialized(NumQuadColumns + 1);
		ColumnU[0] = 0.0;
		double MeanWidth = 0.0;
		for (int32 Column = 0; Column < NumColumns; ++Column)
		{
			MeanWidth += FVector3d::Distance(RailA[Column], RailB[Column]);
		}
		MeanWidth /= NumColumns;
		for (int32 Column = 0; Column < NumQuadColumns; ++Column)
		{
			ColumnU[Column + 1] = ColumnU[Column] +
				FVector3d::Distance(RailA[Column], RailA[(Column + 1) % NumColumns]);
		}

		const int32 NumUVColumns = NumQuadColumns + 1;
		TArray<int32> UVElementIDs;
		UVElementIDs.SetNumUninitialized(NumUVColumns * NumVertexRows);
		for (int32 Row = 0; Row < NumVertexRows; ++Row)
		{
			const double V = MeanWidth * Row / NumRows;
			for (int32 Column = 0; Column < NumUVColumns; ++Column)
			{
				UVElementIDs[Row * NumUVColumns + Column] = UVs->AppendElement(FVector2f(
					static_cast<float>(ColumnU[Column] * Options.UVScale), static_cast<float>(V * Options.UVScale)));
			}
		}

		const auto AppendTriangle = [&](const FIndex3i& Grid, const FIndex3i& UVGrid)
		{
			FIndex3i Triangle(VertexIDs[Grid.A], VertexIDs[Grid.B], VertexIDs[Grid.C]);
			FIndex3i UVTriangle(UVElementIDs[UVGrid.A], UVElementIDs[UVGrid.B], UVElementIDs[UVGrid.C]);
			if (bReverseWinding)
			{
				Swap(Triangle.B, Triangle.C);
				Swap(UVTriangle.B, UVTriangle.C);
			}
			const int32 TriangleID = OutMesh.AppendTriangle(Triangle, GroupID);
			if (TriangleID >= 0)
			{
				UVs->SetTriangle(TriangleID, UVTriangle);
			}
		};

		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			for (int32 Column = 0; Column < NumQuadColumns; ++Column)
			{
				const int32 NextColumn = (Column + 1) % NumColumns;
				const int32 V00 = Row * NumColumns + Column;
				const int32 V10 = Row * NumColumns + NextColumn;
				const int32 V11 = (Row + 1) * NumColumns + NextColumn;
				const int32 V01 = (Row + 1) * NumColumns + Column;

				const int32 U00 = Row * NumUVColumns + Column;
				const int32 U10 = Row * NumUVColumns + Column + 1;
				const int32 U11 = (Row + 1) * NumUVColumns + Column + 1;
				const int32 U01 = (Row + 1) * NumUVColumns + Column;

				// One consistent diagonal throughout, so the surface has no alternating facet pattern.
				AppendTriangle(FIndex3i(V00, V10, V11), FIndex3i(U00, U10, U11));
				AppendTriangle(FIndex3i(V00, V11, V01), FIndex3i(U00, U11, U01));
			}
		}

		int32 NumCapTriangles = 0;
		if (Options.bCapRailB && Options.bClosed)
		{
			// The cap reuses the last row's vertices, so it shares Rail B exactly with the loft: no second seam.
			const FVector3d AxisX = FVector3d::CrossProduct(
				Up, FMath::Abs(Up.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
			const FVector3d AxisY = FVector3d::CrossProduct(Up, AxisX);

			TArray<FVector2d> Polygon;
			Polygon.SetNumUninitialized(NumColumns);
			TArray<int32> CapUVElementIDs;
			CapUVElementIDs.SetNumUninitialized(NumColumns);
			for (int32 Column = 0; Column < NumColumns; ++Column)
			{
				const FVector3d& Position = RailB[Column];
				Polygon[Column] = FVector2d(FVector3d::DotProduct(Position, AxisX), FVector3d::DotProduct(Position, AxisY));
				CapUVElementIDs[Column] = UVs->AppendElement(FVector2f(
					static_cast<float>(Polygon[Column].X * Options.UVScale), static_cast<float>(Polygon[Column].Y * Options.UVScale)));
			}

			// Constrained Delaunay rather than ear clipping: a rail read off a mesh edge has runs of collinear
			// vertices, and ear clipping turns three collinear vertices into a zero-area triangle.
			TArray<FIndex3i> CapTriangles;
			TArray<FIndex2i> CapEdges;
			CapEdges.Reserve(NumColumns);
			for (int32 Column = 0; Column < NumColumns; ++Column)
			{
				CapEdges.Add(FIndex2i(Column, (Column + 1) % NumColumns));
			}
			FDelaunay2 Delaunay;
			if (!Delaunay.Triangulate(Polygon, CapEdges) ||
				!Delaunay.GetFilledTriangles(CapTriangles, CapEdges, FDelaunay2::EFillMode::Solid) ||
				CapTriangles.IsEmpty())
			{
				CapTriangles.Reset();
				PolygonTriangulation::TriangulateSimplePolygon<double>(Polygon, CapTriangles, false);
			}

			const int32 CapGroupID = OutMesh.AllocateTriangleGroup();
			for (FIndex3i Cap : CapTriangles)
			{
				const FVector3d Normal = FVector3d::CrossProduct(RailB[Cap.C] - RailB[Cap.A], RailB[Cap.B] - RailB[Cap.A]);
				if ((FVector3d::DotProduct(Normal, Up) < 0.0) != Options.bFlipFaces)
				{
					Swap(Cap.B, Cap.C);
				}
				const int32 TriangleID = OutMesh.AppendTriangle(
					FIndex3i(OutResultRailB(Cap.A), OutResultRailB(Cap.B), OutResultRailB(Cap.C)), CapGroupID);
				if (TriangleID >= 0)
				{
					UVs->SetTriangle(TriangleID, FIndex3i(CapUVElementIDs[Cap.A], CapUVElementIDs[Cap.B], CapUVElementIDs[Cap.C]));
					++NumCapTriangles;
				}
			}
		}
		OutResult.NumCapTriangles = NumCapTriangles;

		FMeshNormals::InitializeOverlayToPerVertexNormals(OutMesh.Attributes()->PrimaryNormals(), false);

		OutResult.NumColumns = NumColumns;
		OutResult.NumRows = NumRows;
		OutResult.RailAVertexIDs.SetNumUninitialized(NumColumns);
		OutResult.RailBVertexIDs.SetNumUninitialized(NumColumns);
		for (int32 Column = 0; Column < NumColumns; ++Column)
		{
			OutResult.RailAVertexIDs[Column] = VertexIDs[Column];
			OutResult.RailBVertexIDs[Column] = VertexIDs[NumRows * NumColumns + Column];
		}
		return true;
	}

	FOffsetResult OffsetRail(
		TConstArrayView<FVector3d> Rail, const FOffsetOptions& Options, TArray<FVector3d>& OutRail)
	{
		FOffsetResult Result;
		const int32 Num = Rail.Num();
		OutRail.SetNumUninitialized(Num);
		if (Num < 2)
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				OutRail[Index] = Rail[Index];
			}
			return Result;
		}

		const FVector3d Up = LoftPrivate::SafeUpAxis(Options.UpAxis);
		const bool bClosed = Options.bClosed && Num >= 3;

		double Distance = Options.Distance;
		if (bClosed && Options.bPositiveIsOutward && LoftPrivate::SignedTwiceArea(Rail, Up) < 0.0)
		{
			// Direction x Up points away from a loop of positive signed area and into one of negative area.
			Distance = -Distance;
		}

		const double MiterLimit = FMath::Max(1.0, Options.MiterLimit);
		const int32 NumSegments = bClosed ? Num : Num - 1;

		// Smoothed offset directions: each point's direction is the length-weighted average of the segment normals
		// within SmoothingDistance of it along the rail. A dense rail turns a corner over one or two short
		// segments, and a mitered offset reverses those segments as soon as they are shorter than the offset
		// can absorb; spreading the turn over a window keeps the offset rail running the same way as the source.
		TArray<FVector3d> SmoothedNormals;
		if (Options.SmoothingDistance > 0.0)
		{
			TArray<FVector3d> SegmentNormals;
			TArray<double> SegmentLengths;
			SegmentNormals.SetNumUninitialized(NumSegments);
			SegmentLengths.SetNumUninitialized(NumSegments);
			for (int32 Segment = 0; Segment < NumSegments; ++Segment)
			{
				const FVector3d Flat = LoftPrivate::Flatten(Rail[(Segment + 1) % Num] - Rail[Segment], Up);
				SegmentLengths[Segment] = Flat.Length();
				SegmentNormals[Segment] = FVector3d::CrossProduct(Flat.GetSafeNormal(), Up);
			}

			SmoothedNormals.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				FVector3d Sum = FVector3d::ZeroVector;
				// Forward from this point, then backward; a closed rail wraps, an open one stops at its ends.
				for (int32 Direction = 0; Direction < 2; ++Direction)
				{
					double Remaining = Options.SmoothingDistance;
					for (int32 Step = 0; Step < NumSegments && Remaining > 0.0; ++Step)
					{
						int32 Segment = (Direction == 0) ? Index + Step : Index - 1 - Step;
						if (bClosed)
						{
							Segment = ((Segment % NumSegments) + NumSegments) % NumSegments;
						}
						else if (Segment < 0 || Segment >= NumSegments)
						{
							break;
						}
						const double Used = FMath::Min(SegmentLengths[Segment], Remaining);
						Sum += SegmentNormals[Segment] * Used;
						Remaining -= Used;
					}
				}
				SmoothedNormals[Index] = Sum.GetSafeNormal();
			}
		}

		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (SmoothedNormals.Num() == Num && !SmoothedNormals[Index].IsNearlyZero())
			{
				OutRail[Index] = Rail[Index] + SmoothedNormals[Index] * Distance + Up * Options.Height;
				continue;
			}

			const bool bHasPrevious = bClosed || Index > 0;
			const bool bHasNext = bClosed || Index < Num - 1;
			FVector3d Previous = bHasPrevious
				? LoftPrivate::FlatSegmentDirection(Rail, (Index + Num - 1) % Num, Up) : FVector3d::ZeroVector;
			FVector3d Next = bHasNext
				? LoftPrivate::FlatSegmentDirection(Rail, Index, Up) : FVector3d::ZeroVector;
			if (Previous.IsNearlyZero()) Previous = Next;
			if (Next.IsNearlyZero()) Next = Previous;

			const FVector3d PreviousNormal = FVector3d::CrossProduct(Previous, Up);
			const FVector3d NextNormal = FVector3d::CrossProduct(Next, Up);
			FVector3d Bisector = (PreviousNormal + NextNormal).GetSafeNormal();
			double Scale = 1.0;
			if (Bisector.IsNearlyZero())
			{
				// A full reversal has no bisector; fall back to the incoming side.
				Bisector = PreviousNormal;
			}
			else
			{
				const double CosHalfAngle = FVector3d::DotProduct(Bisector, NextNormal);
				Scale = (CosHalfAngle > UE_DOUBLE_KINDA_SMALL_NUMBER)
					? FMath::Min(1.0 / CosHalfAngle, MiterLimit) : MiterLimit;
			}

			OutRail[Index] = Rail[Index] + Bisector * (Distance * Scale) + Up * Options.Height;
		}

		for (int32 Segment = 0; Segment < NumSegments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Num;
			const FVector3d Source = LoftPrivate::Flatten(Rail[Next] - Rail[Segment], Up);
			const FVector3d Offset = LoftPrivate::Flatten(OutRail[Next] - OutRail[Segment], Up);
			if (!Source.IsNearlyZero() && FVector3d::DotProduct(Source, Offset) < 0.0)
			{
				if (Result.NumInvertedSegments++ == 0)
				{
					Result.FirstInvertedSegment = Segment;
				}
			}
		}
		return Result;
	}

	void AlignRail(
		TConstArrayView<FVector3d> Reference, bool bClosed, const FVector3d& UpAxis, TArray<FVector3d>& InOutRail)
	{
		const int32 Num = InOutRail.Num();
		if (Reference.Num() < 2 || Num < 2)
		{
			return;
		}

		if (bClosed && Reference.Num() >= 3 && Num >= 3)
		{
			const FVector3d Up = LoftPrivate::SafeUpAxis(UpAxis);
			if ((LoftPrivate::SignedTwiceArea(Reference, Up) < 0.0) != (LoftPrivate::SignedTwiceArea(InOutRail, Up) < 0.0))
			{
				Algo::Reverse(InOutRail);
			}

			int32 Nearest = 0;
			double NearestDistSq = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const double DistSq = FVector3d::DistSquared(InOutRail[Index], Reference[0]);
				if (DistSq < NearestDistSq)
				{
					NearestDistSq = DistSq;
					Nearest = Index;
				}
			}
			if (Nearest > 0)
			{
				TArray<FVector3d> Rotated;
				Rotated.Reserve(Num);
				for (int32 Index = 0; Index < Num; ++Index)
				{
					Rotated.Add(InOutRail[(Nearest + Index) % Num]);
				}
				InOutRail = MoveTemp(Rotated);
			}
			return;
		}

		const double Straight = FVector3d::DistSquared(Reference[0], InOutRail[0]) +
			FVector3d::DistSquared(Reference.Last(), InOutRail.Last());
		const double Crossed = FVector3d::DistSquared(Reference[0], InOutRail.Last()) +
			FVector3d::DistSquared(Reference.Last(), InOutRail[0]);
		if (Crossed < Straight)
		{
			Algo::Reverse(InOutRail);
		}
	}

	void ResampleByArcLength(
		TConstArrayView<FVector3d> Rail, bool bClosed, int32 Count, TArray<FVector3d>& OutRail)
	{
		OutRail.Reset();
		const int32 Num = Rail.Num();
		if (Num == 0 || Count <= 0)
		{
			return;
		}

		const int32 NumSegments = (bClosed && Num >= 3) ? Num : Num - 1;
		TArray<double> Cumulative;
		Cumulative.SetNumUninitialized(NumSegments + 1);
		Cumulative[0] = 0.0;
		for (int32 Segment = 0; Segment < NumSegments; ++Segment)
		{
			Cumulative[Segment + 1] = Cumulative[Segment] +
				FVector3d::Distance(Rail[Segment], Rail[(Segment + 1) % Num]);
		}
		const double TotalLength = Cumulative[NumSegments];

		OutRail.SetNumUninitialized(Count);
		if (TotalLength <= UE_DOUBLE_SMALL_NUMBER || NumSegments < 1)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				OutRail[Index] = Rail[0];
			}
			return;
		}

		// A closed rail's last sample stops one step short of the start; an open rail's lands on its end point.
		const int32 Steps = (bClosed && Num >= 3) ? Count : FMath::Max(1, Count - 1);
		int32 Segment = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Target = FMath::Min(TotalLength * Index / Steps, TotalLength);
			while (Segment < NumSegments - 1 && Cumulative[Segment + 1] < Target)
			{
				++Segment;
			}
			const double SegmentLength = Cumulative[Segment + 1] - Cumulative[Segment];
			const double Alpha = (SegmentLength > UE_DOUBLE_SMALL_NUMBER)
				? FMath::Clamp((Target - Cumulative[Segment]) / SegmentLength, 0.0, 1.0) : 0.0;
			OutRail[Index] = FMath::Lerp(Rail[Segment], Rail[(Segment + 1) % Num], Alpha);
		}
	}
}
