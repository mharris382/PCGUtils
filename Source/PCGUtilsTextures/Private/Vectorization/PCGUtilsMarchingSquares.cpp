// Copyright Max Harris
// Marching squares iso-contouring (Lorensen & Cline, "Marching Cubes", SIGGRAPH 1987, in its 2D form), with the
// cell table and saddle handling scikit-image's find_contours documents.

#include "Vectorization/PCGUtilsVectorization.h"
#include "Vectorization/PCGUtilsVectorizationInternal.h"

namespace PCGUtilsVectorization
{
	namespace
	{
		enum class EMarchingSquaresEdge : uint8 { Top, Right, Bottom, Left };

		/**
		 * The crossings of one cell, as pairs of edges. Corner bits: top-left 8, top-right 4, bottom-right 2,
		 * bottom-left 1. Saddles (5, 10) are resolved by the caller.
		 */
		int32 MarchingSquaresCellSegments(int32 Case, bool bSaddleJoinsForeground, EMarchingSquaresEdge (&OutPairs)[2][2])
		{
			using E = EMarchingSquaresEdge;
			auto Set = [&OutPairs](int32 Slot, E A, E B) { OutPairs[Slot][0] = A; OutPairs[Slot][1] = B; };
			switch (Case)
			{
			case 1: case 14: Set(0, E::Left, E::Bottom); return 1;
			case 2: case 13: Set(0, E::Bottom, E::Right); return 1;
			case 3: case 12: Set(0, E::Left, E::Right); return 1;
			case 4: case 11: Set(0, E::Top, E::Right); return 1;
			case 6: case 9: Set(0, E::Top, E::Bottom); return 1;
			case 7: case 8: Set(0, E::Left, E::Top); return 1;
			case 5:
				// Top-right and bottom-left inside. Joined: cut off the two outside corners; apart: the inside ones.
				if (bSaddleJoinsForeground) { Set(0, E::Left, E::Top); Set(1, E::Right, E::Bottom); }
				else { Set(0, E::Top, E::Right); Set(1, E::Left, E::Bottom); }
				return 2;
			case 10:
				// Top-left and bottom-right inside.
				if (bSaddleJoinsForeground) { Set(0, E::Top, E::Right); Set(1, E::Left, E::Bottom); }
				else { Set(0, E::Left, E::Top); Set(1, E::Right, E::Bottom); }
				return 2;
			default:
				return 0;
			}
		}
	}

	namespace Internal
	{
		void TraceIsoContours(
			const FPCGUtilsGreyscaleImage& Image, float Level, bool bInterpolate, EPCGUtilsMarchingSquaresSaddle Saddle,
			TArray<TArray<FVector2D>>& OutLoops)
		{
			OutLoops.Reset();
			if (!Image.IsValid())
			{
				return;
			}

			const int32 W = Image.Width;
			const int32 H = Image.Height;
			// Background around the image closes every contour. 0 puts a binary edge exactly on the image border.
			const float Outside = Level > 0.0f ? 0.0f : Level - 1.0f;
			auto Sample = [&](int32 X, int32 Y) { return Image.AtOrDefault(X, Y, Outside); };
			auto IsInside = [&](int32 X, int32 Y) { return Sample(X, Y) > Level; };

			// Samples sit at pixel centers; the padded grid runs from -1 to W (and -1 to H).
			const int64 Stride = W + 2;
			auto HorizontalKey = [Stride](int32 X, int32 Y) { return ((static_cast<int64>(Y) + 1) * Stride + (X + 1)) * 2; };
			auto VerticalKey = [Stride](int32 X, int32 Y) { return ((static_cast<int64>(Y) + 1) * Stride + (X + 1)) * 2 + 1; };

			TMap<int64, FVector2D> EdgePoints;
			auto CrossingPoint = [&](int32 AX, int32 AY, int32 BX, int32 BY) -> FVector2D
			{
				const float A = Sample(AX, AY), B = Sample(BX, BY);
				const double T = bInterpolate && !FMath::IsNearlyEqual(A, B) ? FMath::Clamp((Level - A) / (B - A), 0.0f, 1.0f) : 0.5;
				return FMath::Lerp(FVector2D(AX + 0.5, AY + 0.5), FVector2D(BX + 0.5, BY + 0.5), T);
			};
			auto EdgeKey = [&](EMarchingSquaresEdge Edge, int32 CX, int32 CY) -> int64
			{
				int64 Key = 0;
				switch (Edge)
				{
				case EMarchingSquaresEdge::Top: Key = HorizontalKey(CX, CY); if (!EdgePoints.Contains(Key)) { EdgePoints.Add(Key, CrossingPoint(CX, CY, CX + 1, CY)); } break;
				case EMarchingSquaresEdge::Bottom: Key = HorizontalKey(CX, CY + 1); if (!EdgePoints.Contains(Key)) { EdgePoints.Add(Key, CrossingPoint(CX, CY + 1, CX + 1, CY + 1)); } break;
				case EMarchingSquaresEdge::Left: Key = VerticalKey(CX, CY); if (!EdgePoints.Contains(Key)) { EdgePoints.Add(Key, CrossingPoint(CX, CY, CX, CY + 1)); } break;
				case EMarchingSquaresEdge::Right: Key = VerticalKey(CX + 1, CY); if (!EdgePoints.Contains(Key)) { EdgePoints.Add(Key, CrossingPoint(CX + 1, CY, CX + 1, CY + 1)); } break;
				}
				return Key;
			};

			TArray<TPair<int64, int64>> Segments;
			TMap<int64, TArray<int32, TInlineAllocator<2>>> SegmentsAtEdge;
			for (int32 CY = -1; CY < H; ++CY)
			{
				for (int32 CX = -1; CX < W; ++CX)
				{
					const int32 Case = (IsInside(CX, CY) ? 8 : 0) | (IsInside(CX + 1, CY) ? 4 : 0)
						| (IsInside(CX + 1, CY + 1) ? 2 : 0) | (IsInside(CX, CY + 1) ? 1 : 0);
					if (Case == 0 || Case == 15)
					{
						continue;
					}
					bool bJoin = Saddle == EPCGUtilsMarchingSquaresSaddle::ConnectForeground;
					if (Saddle == EPCGUtilsMarchingSquaresSaddle::Average)
					{
						bJoin = (Sample(CX, CY) + Sample(CX + 1, CY) + Sample(CX + 1, CY + 1) + Sample(CX, CY + 1)) * 0.25f > Level;
					}
					EMarchingSquaresEdge Pairs[2][2];
					const int32 Count = MarchingSquaresCellSegments(Case, bJoin, Pairs);
					for (int32 Slot = 0; Slot < Count; ++Slot)
					{
						const int64 A = EdgeKey(Pairs[Slot][0], CX, CY);
						const int64 B = EdgeKey(Pairs[Slot][1], CX, CY);
						const int32 SegmentIndex = Segments.Emplace(A, B);
						SegmentsAtEdge.FindOrAdd(A).Add(SegmentIndex);
						SegmentsAtEdge.FindOrAdd(B).Add(SegmentIndex);
					}
				}
			}

			// Every crossed edge is shared by exactly two segments (one per adjacent cell), so the segments form loops.
			TBitArray<> Used(false, Segments.Num());
			for (int32 Start = 0; Start < Segments.Num(); ++Start)
			{
				if (Used[Start])
				{
					continue;
				}
				TArray<FVector2D>& Loop = OutLoops.AddDefaulted_GetRef();
				int32 Current = Start;
				int64 From = Segments[Start].Key;
				while (Current != INDEX_NONE && !Used[Current])
				{
					Used[Current] = true;
					Loop.Add(EdgePoints[From]);
					const int64 To = Segments[Current].Key == From ? Segments[Current].Value : Segments[Current].Key;
					int32 Next = INDEX_NONE;
					for (const int32 Candidate : SegmentsAtEdge[To])
					{
						if (Candidate != Current && !Used[Candidate])
						{
							Next = Candidate;
							break;
						}
					}
					From = To;
					Current = Next;
				}
			}
		}
	}

	void MarchingSquares(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsMarchingSquaresOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths)
	{
		OutPaths.Reset();
		TArray<TArray<FVector2D>> Loops;
		Internal::TraceIsoContours(Image, Options.IsoLevel, Options.bInterpolate, Options.Saddle, Loops);
		OutPaths.Reserve(Loops.Num());
		for (TArray<FVector2D>& Loop : Loops)
		{
			SimplifyDouglasPeucker(Loop, Options.SimplifyTolerance, /*bClosed=*/true);
			OutPaths.AddDefaulted_GetRef().Points = MoveTemp(Loop);
		}
		Internal::FinalizePaths(OutPaths, Options.MinArea, Options.Retrieval, /*bHierarchyKnown=*/false);
	}
}
