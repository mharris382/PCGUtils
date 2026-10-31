// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshUVPack.h"

#include "CompGeom/FitOrientedBox2.h"
#include "DynamicMesh/DynamicMesh3.h"

namespace PCGUtilsDynMeshUVPack
{
	namespace
	{
		/** One island's box, in its own (possibly turned) frame with its minimum corner at the origin. */
		struct FRect
		{
			int32 Island = INDEX_NONE;
			double W = 0.0;
			double H = 0.0;
		};

		struct FPlacement
		{
			double X = 0.0;
			double Y = 0.0;
			bool bTurned = false;
		};

		struct FSegment
		{
			double X = 0.0;
			double Y = 0.0;
			double W = 0.0;
		};

		constexpr double Epsilon = 1e-9;

		/** The lowest height a W-wide box can sit at starting on segment Index, or false if it overhangs BinW. */
		bool HeightAt(const TArray<FSegment>& Sky, int32 Index, double W, double BinW, double& OutY)
		{
			const double X = Sky[Index].X;
			if (X + W > BinW + Epsilon)
			{
				return false;
			}
			double Y = 0.0;
			double Left = W;
			for (int32 Segment = Index; Segment < Sky.Num() && Left > Epsilon; ++Segment)
			{
				Y = FMath::Max(Y, Sky[Segment].Y);
				Left -= Sky[Segment].W;
			}
			OutY = Y;
			return Left <= Epsilon;
		}

		void AddToSkyline(TArray<FSegment>& Sky, int32 Index, double W, double Top)
		{
			const double X = Sky[Index].X;
			const double End = X + W;
			Sky.Insert(FSegment{X, Top, W}, Index);
			// Trim the segments the new one covers.
			int32 Next = Index + 1;
			while (Next < Sky.Num() && Sky[Next].X < End - Epsilon)
			{
				const double SegmentEnd = Sky[Next].X + Sky[Next].W;
				if (SegmentEnd <= End + Epsilon)
				{
					Sky.RemoveAt(Next);
				}
				else
				{
					Sky[Next].W = SegmentEnd - End;
					Sky[Next].X = End;
					break;
				}
			}
			// Merge neighbours of equal height.
			for (int32 Segment = 0; Segment + 1 < Sky.Num();)
			{
				if (FMath::IsNearlyEqual(Sky[Segment].Y, Sky[Segment + 1].Y, Epsilon))
				{
					Sky[Segment].W += Sky[Segment + 1].W;
					Sky.RemoveAt(Segment + 1);
				}
				else
				{
					++Segment;
				}
			}
		}

		/**
		 * Places every rect, scaled by Scale and padded by Pad, bottom-left on a skyline BinW wide. Returns the height
		 * used. Rects must be sorted largest first.
		 */
		double Skyline(const TArray<FRect>& Rects, double Scale, double Pad, double BinW, bool bAllowTurn,
			TArray<FPlacement>& OutPlacements)
		{
			TArray<FSegment> Sky;
			Sky.Add(FSegment{0.0, 0.0, BinW});
			OutPlacements.SetNum(Rects.Num());
			double Height = 0.0;
			for (int32 RectIndex = 0; RectIndex < Rects.Num(); ++RectIndex)
			{
				const FRect& Rect = Rects[RectIndex];
				double BestTop = TNumericLimits<double>::Max();
				double BestX = TNumericLimits<double>::Max();
				int32 BestSegment = INDEX_NONE;
				bool bBestTurned = false;
				for (int32 Turn = 0; Turn < (bAllowTurn ? 2 : 1); ++Turn)
				{
					const double W = (Turn ? Rect.H : Rect.W) * Scale + Pad;
					const double H = (Turn ? Rect.W : Rect.H) * Scale + Pad;
					for (int32 Segment = 0; Segment < Sky.Num(); ++Segment)
					{
						double Y = 0.0;
						if (!HeightAt(Sky, Segment, W, BinW, Y))
						{
							continue;
						}
						const double Top = Y + H;
						if (Top < BestTop - Epsilon || (FMath::IsNearlyEqual(Top, BestTop, Epsilon) && Sky[Segment].X < BestX))
						{
							BestTop = Top;
							BestX = Sky[Segment].X;
							BestSegment = Segment;
							bBestTurned = Turn != 0;
						}
					}
				}
				if (BestSegment == INDEX_NONE)
				{
					// Wider than the bin: put it on top of everything, at the left.
					const double W = Rect.W * Scale + Pad;
					const double H = Rect.H * Scale + Pad;
					OutPlacements[RectIndex] = FPlacement{0.0, Height, false};
					Height += H;
					Sky.Reset();
					Sky.Add(FSegment{0.0, Height, FMath::Max(BinW, W)});
					continue;
				}
				const double W = (bBestTurned ? Rect.H : Rect.W) * Scale + Pad;
				const double H = (bBestTurned ? Rect.W : Rect.H) * Scale + Pad;
				OutPlacements[RectIndex] = FPlacement{Sky[BestSegment].X, BestTop - H, bBestTurned};
				AddToSkyline(Sky, BestSegment, W, BestTop);
				Height = FMath::Max(Height, BestTop);
			}
			return Height;
		}

		FVector2d Centroid(const TArray<FVector2f>& UVs, const FIsland& Island)
		{
			FVector2d Sum = FVector2d::ZeroVector;
			for (const int32 Index : Island.UVIndices)
			{
				Sum += FVector2d(UVs[Index]);
			}
			return Island.UVIndices.IsEmpty() ? Sum : Sum / Island.UVIndices.Num();
		}
	}

	FResult Pack(TArray<FVector2f>& UVs, TArrayView<const FIsland> Islands, const FOptions& Options)
	{
		FResult Result;
		if (Islands.IsEmpty())
		{
			return Result;
		}

		// 1. Average Island Scale: equal UV area per surface area, total UV area kept.
		if (Options.bAverageIslandScale)
		{
			double TotalSurface = 0.0;
			double TotalUV = 0.0;
			for (const FIsland& Island : Islands)
			{
				TotalSurface += Island.SurfaceArea;
				TotalUV += Island.UVArea;
			}
			if (TotalSurface > Epsilon && TotalUV > Epsilon)
			{
				const double Density = TotalUV / TotalSurface;
				for (const FIsland& Island : Islands)
				{
					if (Island.UVArea <= Epsilon || Island.SurfaceArea <= Epsilon)
					{
						continue;
					}
					const double Factor = FMath::Sqrt(Density * Island.SurfaceArea / Island.UVArea);
					const FVector2d Center = Centroid(UVs, Island);
					for (const int32 Index : Island.UVIndices)
					{
						UVs[Index] = FVector2f(Center + (FVector2d(UVs[Index]) - Center) * Factor);
					}
				}
			}
		}

		// 2. Turn islands to their smallest-area box, then move each to the origin.
		const bool bAlign = Options.Rotation == ERotation::AxisAligned || Options.Rotation == ERotation::Any;
		TArray<FRect> Rects;
		Rects.Reserve(Islands.Num());
		double PrePackArea = 0.0;
		for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
		{
			const FIsland& Island = Islands[IslandIndex];
			if (Island.UVIndices.IsEmpty())
			{
				continue;
			}
			if (bAlign && Island.UVIndices.Num() >= 3)
			{
				TArray<FVector2d> Points;
				Points.Reserve(Island.UVIndices.Num());
				for (const int32 Index : Island.UVIndices)
				{
					Points.Add(FVector2d(UVs[Index]));
				}
				const UE::Geometry::TOrientedBox2<double> Box = UE::Geometry::FitOrientedBox2Points<double>(Points);
				const FVector2d AxisX = Box.UnitAxisX;
				// Rotate so the box's X axis lies along U: multiply by the inverse rotation.
				for (const int32 Index : Island.UVIndices)
				{
					const FVector2d P = FVector2d(UVs[Index]) - Box.Origin;
					UVs[Index] = FVector2f(FVector2d(P.X * AxisX.X + P.Y * AxisX.Y, -P.X * AxisX.Y + P.Y * AxisX.X));
				}
			}
			FBox2d Bounds(ForceInit);
			for (const int32 Index : Island.UVIndices)
			{
				Bounds += FVector2d(UVs[Index]);
			}
			for (const int32 Index : Island.UVIndices)
			{
				UVs[Index] = FVector2f(FVector2d(UVs[Index]) - Bounds.Min);
			}
			const FVector2d Size = Bounds.GetSize();
			Rects.Add(FRect{IslandIndex, FMath::Max(Size.X, 0.0), FMath::Max(Size.Y, 0.0)});
			PrePackArea += Size.X * Size.Y;
		}
		if (Rects.IsEmpty() || PrePackArea <= Epsilon * Epsilon)
		{
			return Result;
		}
		Rects.Sort([](const FRect& A, const FRect& B)
		{
			const double MaxA = FMath::Max(A.W, A.H);
			const double MaxB = FMath::Max(B.W, B.H);
			return MaxA != MaxB ? MaxA > MaxB : A.W * A.H > B.W * B.H;
		});

		const bool bAllowTurn = Options.Rotation == ERotation::Cardinal || Options.Rotation == ERotation::Any;
		const double Margin = FMath::Max(Options.Margin, 0.0);
		const double ScaledReference = FMath::Sqrt(PrePackArea);
		auto PadFor = [&](double Scale)
		{
			switch (Options.MarginMethod)
			{
			case EMargin::Fraction: return Margin;
			case EMargin::Add: return Margin * Scale;
			case EMargin::Scaled: default: return Margin * ScaledReference * Scale;
			}
		};

		// 3. Pack. Each box is padded by a full margin and its island sits half a margin in, so neighbours are a
		// margin apart and the square's edges half a margin away. With Scale, bisect the largest scale that fits.
		TArray<FPlacement> Placements;
		double Scale = 1.0;
		if (Options.bScale)
		{
			auto Fits = [&](double TryScale, TArray<FPlacement>& Out)
			{
				return Skyline(Rects, TryScale, PadFor(TryScale), 1.0, bAllowTurn, Out) <= 1.0 + Epsilon;
			};
			double High = 1.0 / FMath::Sqrt(PrePackArea);
			double Low = 0.0;
			TArray<FPlacement> Attempt;
			for (int32 Iteration = 0; Iteration < 40; ++Iteration)
			{
				const double Mid = Iteration == 0 ? High : 0.5 * (Low + High);
				if (Fits(Mid, Attempt))
				{
					Low = Mid;
					Placements = Attempt;
					if (Iteration == 0)
					{
						break;
					}
				}
				else
				{
					High = Mid;
				}
			}
			if (Placements.IsEmpty())
			{
				// Margins alone fill the square; pack as small as the bisection got.
				Low = FMath::Max(Low, High * 1e-3);
				Fits(Low, Placements);
			}
			Scale = Low;
		}
		else
		{
			double MaxWidth = 1.0;
			for (const FRect& Rect : Rects)
			{
				MaxWidth = FMath::Max(MaxWidth, FMath::Min(Rect.W, Rect.H) + PadFor(1.0));
			}
			Skyline(Rects, 1.0, PadFor(1.0), MaxWidth, bAllowTurn, Placements);
		}

		const double Pad = PadFor(Scale);
		for (int32 RectIndex = 0; RectIndex < Rects.Num(); ++RectIndex)
		{
			const FRect& Rect = Rects[RectIndex];
			const FPlacement& Place = Placements[RectIndex];
			for (const int32 Index : Islands[Rect.Island].UVIndices)
			{
				FVector2d P(UVs[Index]);
				if (Place.bTurned)
				{
					// A quarter turn counter-clockwise, kept in the positive quadrant.
					P = FVector2d(Rect.H - P.Y, P.X);
				}
				UVs[Index] = FVector2f(P * Scale + FVector2d(Place.X + 0.5 * Pad, Place.Y + 0.5 * Pad));
			}
			Result.Rotated += Place.bTurned ? 1 : 0;
		}
		Result.Scale = Scale;
		Result.bPacked = true;
		return Result;
	}

	FRegionResult PackRegion(const UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& Overlay,
		TArrayView<const int32> TriangleIDs, const FOptions& Options)
	{
		FRegionResult Result;
		PCGUtilsDynMeshUV::FRegionUVs Region;
		PCGUtilsDynMeshUV::Gather(Mesh, Overlay, TriangleIDs, Region);
		Result.Unset = Region.NumUnset;
		const TArray<TArray<int32>> IslandTriangles = PCGUtilsDynMeshUV::FindIslands(Region);
		TArray<FIsland> Islands;
		Islands.Reserve(IslandTriangles.Num());
		for (const TArray<int32>& Triangles : IslandTriangles)
		{
			FIsland& Island = Islands.AddDefaulted_GetRef();
			Island.UVIndices = PCGUtilsDynMeshUV::IslandUVIndices(Region, Triangles);
			PCGUtilsDynMeshUV::IslandAreas(Mesh, Region, Triangles, Island.SurfaceArea, Island.UVArea);
		}
		Result.Islands = Islands.Num();
		Result.Pack = Pack(Region.UVs, Islands, Options);
		if (Result.Pack.bPacked)
		{
			PCGUtilsDynMeshUV::WriteBack(Overlay, Region);
		}
		return Result;
	}
}
