// Copyright Max Harris
// Topological border following: S. Suzuki and K. Abe, "Topological Structural Analysis of Digitized Binary Images
// by Border Following", CVGIP 30(1), 1985 - Algorithm 1, the method OpenCV's findContours implements.

#include "Vectorization/PCGUtilsVectorization.h"
#include "Vectorization/PCGUtilsVectorizationInternal.h"

namespace PCGUtilsVectorization
{
	namespace
	{
		// The 8 neighbours, clockwise on screen (Y down) starting east: E, SE, S, SW, W, NW, N, NE.
		const int32 BorderDX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
		const int32 BorderDY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

		int32 BorderDirectionTo(int32 FromX, int32 FromY, int32 ToX, int32 ToY)
		{
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				if (FromX + BorderDX[Direction] == ToX && FromY + BorderDY[Direction] == ToY)
				{
					return Direction;
				}
			}
			return 0;
		}

		/** Keeps only the chain points where the step direction changes (OpenCV CHAIN_APPROX_SIMPLE). */
		void BorderChainApproxSimple(TArray<FVector2D>& Points)
		{
			const int32 Num = Points.Num();
			if (Num < 3)
			{
				return;
			}
			TArray<FVector2D> Kept;
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FVector2D& Prev = Points[(Index + Num - 1) % Num];
				const FVector2D& Next = Points[(Index + 1) % Num];
				const FVector2D In = Points[Index] - Prev;
				const FVector2D Out = Next - Points[Index];
				if (!FMath::IsNearlyZero(FVector2D::CrossProduct(In, Out)) || FVector2D::DotProduct(In, Out) < 0.0)
				{
					Kept.Add(Points[Index]);
				}
			}
			Points = Kept.Num() >= 3 ? MoveTemp(Kept) : Points;
		}
	}

	void BorderFollowing(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsBorderFollowingOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths)
	{
		OutPaths.Reset();
		if (!Image.IsValid())
		{
			return;
		}

		// f, framed by a row and column of zeros: 1 for foreground, then overwritten with border numbers.
		const int32 W = Image.Width + 2;
		const int32 H = Image.Height + 2;
		TArray<int32> F;
		F.Init(0, W * H);
		for (int32 Y = 0; Y < Image.Height; ++Y)
		{
			for (int32 X = 0; X < Image.Width; ++X)
			{
				F[(Y + 1) * W + (X + 1)] = Image.At(X, Y) >= Options.Threshold ? 1 : 0;
			}
		}
		auto At = [&F, W](int32 X, int32 Y) -> int32& { return F[Y * W + X]; };

		// Border bookkeeping by border number (NBD). Number 1 is the frame, a hole border with no parent.
		TArray<bool> BorderIsHole = {false, true};
		TArray<int32> BorderPath = {INDEX_NONE, INDEX_NONE};
		TArray<int32> BorderParent = {INDEX_NONE, INDEX_NONE};
		int32 NBD = 1;

		for (int32 Y = 1; Y < H - 1; ++Y)
		{
			int32 LNBD = 1;
			for (int32 X = 1; X < W - 1; ++X)
			{
				const int32 Value = At(X, Y);
				if (Value == 0)
				{
					continue;
				}

				// (1) Is this the start of an outer border, a hole border, or neither?
				bool bHole = false;
				int32 FromX = 0, FromY = 0;
				if (Value == 1 && At(X - 1, Y) == 0)
				{
					FromX = X - 1; FromY = Y;
				}
				else if (Value >= 1 && At(X + 1, Y) == 0)
				{
					bHole = true;
					FromX = X + 1; FromY = Y;
					if (Value > 1) { LNBD = Value; }
				}
				else
				{
					if (Value != 1) { LNBD = FMath::Abs(Value); }
					continue;
				}

				++NBD;
				BorderIsHole.Add(bHole);
				// (2) Parent from the border last met on this row (Suzuki & Abe, Table 1).
				const bool bLastIsHole = BorderIsHole[LNBD];
				BorderParent.Add(bHole == bLastIsHole ? BorderParent[LNBD] : LNBD);

				// (3) Follow the border.
				TArray<FVector2D> Chain;
				// (3.1) Clockwise around (X, Y) from the start neighbour, for any nonzero pixel.
				const int32 StartDirection = BorderDirectionTo(X, Y, FromX, FromY);
				int32 FirstX = INDEX_NONE, FirstY = INDEX_NONE;
				for (int32 Step = 0; Step < 8; ++Step)
				{
					const int32 Direction = (StartDirection + Step) % 8;
					if (At(X + BorderDX[Direction], Y + BorderDY[Direction]) != 0)
					{
						FirstX = X + BorderDX[Direction];
						FirstY = Y + BorderDY[Direction];
						break;
					}
				}
				if (FirstX == INDEX_NONE)
				{
					// An isolated pixel: a border of one point, which encloses nothing.
					At(X, Y) = -NBD;
					Chain.Emplace(X - 0.5, Y - 0.5);
				}
				else
				{
					// (3.2)
					int32 PrevX = FirstX, PrevY = FirstY;
					int32 CurX = X, CurY = Y;
					while (true)
					{
						Chain.Emplace(CurX - 0.5, CurY - 0.5);
						// (3.3) Counter-clockwise around the current pixel, starting after the previous one.
						const int32 PrevDirection = BorderDirectionTo(CurX, CurY, PrevX, PrevY);
						bool bExaminedEastZero = false;
						int32 NextX = PrevX, NextY = PrevY;
						for (int32 Step = 1; Step <= 8; ++Step)
						{
							const int32 Direction = (PrevDirection - Step + 8) % 8;
							const int32 NX = CurX + BorderDX[Direction], NY = CurY + BorderDY[Direction];
							if (At(NX, NY) != 0)
							{
								NextX = NX; NextY = NY;
								break;
							}
							if (Direction == 0)
							{
								bExaminedEastZero = true;
							}
						}
						// (3.4)
						if (bExaminedEastZero)
						{
							At(CurX, CurY) = -NBD;
						}
						else if (At(CurX, CurY) == 1)
						{
							At(CurX, CurY) = NBD;
						}
						// (3.5) Back at the start, about to retrace the first step: done.
						if (NextX == X && NextY == Y && CurX == FirstX && CurY == FirstY)
						{
							break;
						}
						PrevX = CurX; PrevY = CurY;
						CurX = NextX; CurY = NextY;
					}
					// The loop records the start pixel again just before it closes; drop the repeat.
					if (Chain.Num() > 1 && Chain.Last().Equals(Chain[0]))
					{
						Chain.Pop();
					}
				}

				switch (Options.Approximation)
				{
				case EPCGUtilsContourApproximation::Simple: BorderChainApproxSimple(Chain); break;
				case EPCGUtilsContourApproximation::DouglasPeucker: SimplifyDouglasPeucker(Chain, Options.Epsilon, /*bClosed=*/true); break;
				default: break;
				}

				const int32 PathIndex = OutPaths.Num();
				FPCGUtilsVectorPath& Path = OutPaths.AddDefaulted_GetRef();
				Path.Points = MoveTemp(Chain);
				Path.ParentIndex = BorderParent.Last() != INDEX_NONE ? BorderPath[BorderParent.Last()] : INDEX_NONE;
				BorderPath.Add(PathIndex);

				// (4)
				if (At(X, Y) != 1) { LNBD = FMath::Abs(At(X, Y)); }
			}
		}

		Internal::FinalizePaths(OutPaths, Options.MinArea, Options.Retrieval, /*bHierarchyKnown=*/true);
	}
}
