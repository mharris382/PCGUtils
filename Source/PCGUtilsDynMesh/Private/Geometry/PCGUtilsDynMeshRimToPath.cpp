// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshRimToPath.h"

#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"

namespace PCGUtilsDynMeshRimToPathPrivate
{
	double DistanceToSegment(const FVector3d& Point, const FVector3d& A, const FVector3d& B)
	{
		const FVector3d AB = B - A;
		const double LengthSq = AB.SquaredLength();
		const double T = LengthSq > 0.0 ? FMath::Clamp(FVector3d::DotProduct(Point - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
		return FVector3d::Distance(Point, A + AB * T);
	}

	double DistanceToLoop(const FVector3d& Point, TConstArrayView<FVector3d> Loop)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			Best = FMath::Min(Best, DistanceToSegment(Point, Loop[Index], Loop[(Index + 1) % Loop.Num()]));
		}
		return Best;
	}

	double MeanDistanceToLoop(TConstArrayView<FVector3d> Points, TConstArrayView<FVector3d> Loop)
	{
		double Sum = 0.0;
		for (const FVector3d& Point : Points) { Sum += DistanceToLoop(Point, Loop); }
		return Points.IsEmpty() ? 0.0 : Sum / Points.Num();
	}

	/** Arc length from Loop[0] to each corner; one extra entry at the end holds the whole loop's length. */
	TArray<double> CornerParams(TConstArrayView<FVector3d> Loop)
	{
		TArray<double> Params;
		Params.SetNumUninitialized(Loop.Num() + 1);
		Params[0] = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			Params[Index + 1] = Params[Index] + FVector3d::Distance(Loop[Index], Loop[(Index + 1) % Loop.Num()]);
		}
		return Params;
	}

	FVector3d PointAtParam(TConstArrayView<FVector3d> Loop, const TArray<double>& Params, double U)
	{
		const int32 Num = Loop.Num();
		// Last corner whose param is at or before U.
		int32 Segment = FMath::Clamp(Algo::UpperBound(Params, U) - 1, 0, Num - 1);
		const double Length = Params[Segment + 1] - Params[Segment];
		const double T = Length > 0.0 ? FMath::Clamp((U - Params[Segment]) / Length, 0.0, 1.0) : 0.0;
		return FMath::Lerp(Loop[Segment], Loop[(Segment + 1) % Num], T);
	}
}

namespace PCGUtilsDynMeshRimToPath
{
	FVector3d LoopNormal(TConstArrayView<FVector3d> Loop)
	{
		FVector3d Normal = FVector3d::Zero();
		if (Loop.Num() < 3) { return Normal; }
		const FVector3d Origin = Loop[0];
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			Normal += FVector3d::CrossProduct(Loop[Index] - Origin, Loop[(Index + 1) % Loop.Num()] - Origin);
		}
		return Normal * 0.5;
	}

	double LoopLength(TConstArrayView<FVector3d> Loop)
	{
		double Length = 0.0;
		for (int32 Index = 0; Index < Loop.Num() && Loop.Num() > 1; ++Index)
		{
			Length += FVector3d::Distance(Loop[Index], Loop[(Index + 1) % Loop.Num()]);
		}
		return Length;
	}

	void CleanLoop(TArray<FVector3d>& InOutLoop, double Tolerance)
	{
		TArray<FVector3d> Clean;
		Clean.Reserve(InOutLoop.Num());
		for (const FVector3d& Point : InOutLoop)
		{
			if (Clean.IsEmpty() || FVector3d::Distance(Clean.Last(), Point) > Tolerance) { Clean.Add(Point); }
		}
		while (Clean.Num() > 1 && FVector3d::Distance(Clean.Last(), Clean[0]) <= Tolerance) { Clean.Pop(); }
		InOutLoop = MoveTemp(Clean);
	}

	double ShapeDistance(TConstArrayView<FVector3d> A, TConstArrayView<FVector3d> B)
	{
		if (A.IsEmpty() || B.IsEmpty()) { return TNumericLimits<double>::Max(); }
		return 0.5 * (PCGUtilsDynMeshRimToPathPrivate::MeanDistanceToLoop(A, B) + PCGUtilsDynMeshRimToPathPrivate::MeanDistanceToLoop(B, A));
	}

	void AlignTargetToRim(TConstArrayView<FVector3d> Rim, TArray<FVector3d>& InOutTarget)
	{
		if (Rim.Num() < 3 || InOutTarget.Num() < 3) { return; }

		// Compared round the rim's own normal, which also handles a hole's rim (wound the other way) correctly.
		if (FVector3d::DotProduct(LoopNormal(Rim), LoopNormal(InOutTarget)) < 0.0)
		{
			Algo::Reverse(InOutTarget);
		}

		int32 Nearest = 0;
		double NearestDistSq = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < InOutTarget.Num(); ++Index)
		{
			const double DistSq = FVector3d::DistSquared(InOutTarget[Index], Rim[0]);
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Index;
			}
		}
		if (Nearest > 0)
		{
			TArray<FVector3d> Rotated;
			Rotated.Reserve(InOutTarget.Num());
			for (int32 Index = 0; Index < InOutTarget.Num(); ++Index)
			{
				Rotated.Add(InOutTarget[(Nearest + Index) % InOutTarget.Num()]);
			}
			InOutTarget = MoveTemp(Rotated);
		}
	}

	double PairRimWithCorners(TConstArrayView<FVector3d> Rim, TConstArrayView<FVector3d> Target, TArray<int32>& OutCorner)
	{
		const int32 N = Rim.Num();
		const int32 M = Target.Num();
		OutCorner.Init(0, N);
		if (N == 0 || M == 0) { return 0.0; }

		// Very large loops would need a very large table; spread the rim evenly over the target instead.
		if (static_cast<int64>(N) * M > 16ll * 1024 * 1024)
		{
			for (int32 Index = 0; Index < N; ++Index)
			{
				OutCorner[Index] = static_cast<int32>(static_cast<int64>(Index) * M / N);
			}
			double Cost = 0.0;
			for (int32 Index = 0; Index < N; ++Index) { Cost += FVector3d::Distance(Rim[Index], Target[OutCorner[Index]]); }
			return Cost;
		}

		// Sharing costs one average target edge. A shared corner means a rim vertex spread onto a path edge and a
		// corner inserted elsewhere, so it is worth it only when it brings a rim vertex much nearer. Under a
		// uniform shrink, a rim vertex beside a corner is often a little nearer that corner's image than its own;
		// a cheaper share would break up a path that matches the rim one for one.
		const double ShareCost = LoopLength(Target) / M;
		constexpr double Unreachable = TNumericLimits<double>::Max() * 0.5;

		TArray<double> Previous;
		TArray<double> Current;
		Previous.Init(Unreachable, M);
		Current.Init(Unreachable, M);
		Previous[0] = FVector3d::Distance(Rim[0], Target[0]);

		// Back[Row * M + Column] is the corner the previous rim vertex took on the best route here.
		TArray<int32> Back;
		Back.SetNumUninitialized(N * M);
		for (int32 Column = 0; Column < M; ++Column) { Back[Column] = 0; }

		for (int32 Row = 1; Row < N; ++Row)
		{
			double BestBefore = Unreachable;
			int32 BestBeforeColumn = 0;
			for (int32 Column = 0; Column < M; ++Column)
			{
				const double Share = Previous[Column] + ShareCost;
				double Reach = Unreachable;
				int32 From = Column;
				if (BestBefore < Unreachable && BestBefore <= Share)
				{
					Reach = BestBefore;
					From = BestBeforeColumn;
				}
				else if (Previous[Column] < Unreachable)
				{
					Reach = Share;
				}
				Current[Column] = Reach < Unreachable ? Reach + FVector3d::Distance(Rim[Row], Target[Column]) : Unreachable;
				Back[Row * M + Column] = From;

				if (Previous[Column] < BestBefore)
				{
					BestBefore = Previous[Column];
					BestBeforeColumn = Column;
				}
			}
			Swap(Previous, Current);
		}

		int32 Column = 0;
		for (int32 Candidate = 1; Candidate < M; ++Candidate)
		{
			if (Previous[Candidate] < Previous[Column]) { Column = Candidate; }
		}
		const double Cost = Previous[Column];
		for (int32 Row = N - 1; Row >= 0; --Row)
		{
			OutCorner[Row] = Column;
			Column = Back[Row * M + Column];
		}
		OutCorner[0] = 0;
		return Cost;
	}

	void FitRimToTarget(TConstArrayView<FVector3d> Rim, TArray<FVector3d>& InOutTarget, TArray<int32>& OutCorner)
	{
		AlignTargetToRim(Rim, InOutTarget);
		const int32 M = InOutTarget.Num();
		if (Rim.IsEmpty() || M == 0)
		{
			OutCorner.Init(0, Rim.Num());
			return;
		}

		// After alignment the nearest point is first; the next nearest few are the other candidates.
		TArray<int32> Starts;
		for (int32 Index = 0; Index < M; ++Index) { Starts.Add(Index); }
		Starts.Sort([&](int32 A, int32 B) { return FVector3d::DistSquared(InOutTarget[A], Rim[0]) < FVector3d::DistSquared(InOutTarget[B], Rim[0]); });
		Starts.SetNum(FMath::Min(Starts.Num(), 4));

		double BestCost = TNumericLimits<double>::Max();
		int32 BestStart = 0;
		TArray<int32> Corner;
		TArray<FVector3d> Rotated;
		Rotated.SetNumUninitialized(M);
		for (const int32 Start : Starts)
		{
			for (int32 Index = 0; Index < M; ++Index) { Rotated[Index] = InOutTarget[(Start + Index) % M]; }
			const double Cost = PairRimWithCorners(Rim, Rotated, Corner);
			if (Cost < BestCost)
			{
				BestCost = Cost;
				BestStart = Start;
				OutCorner = Corner;
			}
		}
		if (BestStart != 0)
		{
			for (int32 Index = 0; Index < M; ++Index) { Rotated[Index] = InOutTarget[(BestStart + Index) % M]; }
			InOutTarget = Rotated;
		}
	}

	void PlanInnerRim(TConstArrayView<FVector3d> Rim, TConstArrayView<FVector3d> Target,
		TConstArrayView<int32> Corner, FInnerRimPlan& OutPlan)
	{
		const int32 N = Rim.Num();
		const int32 M = Target.Num();
		OutPlan.RimPositions.SetNumUninitialized(N);
		OutPlan.CornersAfter.Reset();
		OutPlan.CornersAfter.SetNum(N);
		if (N == 0 || M == 0) { return; }

		const TArray<double> Params = PCGUtilsDynMeshRimToPathPrivate::CornerParams(Target);
		TArray<double> RimParams;
		RimParams.SetNumUninitialized(N);
		TArray<bool> Occupied;
		Occupied.Init(false, M);

		for (int32 First = 0; First < N;)
		{
			const int32 Shared = Corner[First];
			int32 Last = First;
			while (Last + 1 < N && Corner[Last + 1] == Shared) { ++Last; }

			// One vertex of the run keeps the corner: the first rim vertex if it is in the run (it anchors the
			// alignment), otherwise the nearest. The others spread along the half-edges either side of it.
			int32 Keeper = First;
			if (First > 0)
			{
				double Nearest = TNumericLimits<double>::Max();
				for (int32 Index = First; Index <= Last; ++Index)
				{
					const double Distance = FVector3d::Distance(Rim[Index], Target[Shared]);
					if (Distance < Nearest)
					{
						Nearest = Distance;
						Keeper = Index;
					}
				}
			}
			const double At = Params[Shared];
			const double HalfBefore = Shared > 0 ? 0.5 * (At - Params[Shared - 1]) : 0.0;
			const double HalfAfter = 0.5 * (Params[Shared + 1] - At);
			const int32 NumBefore = Keeper - First;
			const int32 NumAfter = Last - Keeper;
			for (int32 Index = First; Index < Keeper; ++Index)
			{
				RimParams[Index] = At - HalfBefore * double(Keeper - Index) / double(NumBefore + 1);
			}
			RimParams[Keeper] = At;
			for (int32 Index = Keeper + 1; Index <= Last; ++Index)
			{
				RimParams[Index] = At + HalfAfter * double(Index - Keeper) / double(NumAfter + 1);
			}
			Occupied[Shared] = true;
			First = Last + 1;
		}

		for (int32 Index = 0; Index < N; ++Index)
		{
			OutPlan.RimPositions[Index] = Occupied[Corner[Index]] && RimParams[Index] == Params[Corner[Index]]
				? Target[Corner[Index]] : PCGUtilsDynMeshRimToPathPrivate::PointAtParam(Target, Params, RimParams[Index]);
		}

		// Every corner no rim vertex sits on goes between the two rim vertices whose params bracket it.
		int32 Rim0 = 0;
		for (int32 Index = 1; Index < M; ++Index)
		{
			if (Occupied[Index]) { continue; }
			while (Rim0 + 1 < N && RimParams[Rim0 + 1] < Params[Index]) { ++Rim0; }
			OutPlan.CornersAfter[Rim0].Add(Index);
		}
	}

	int32 ChooseFanSplit(const FVector3d& A, const FVector3d& B, TConstArrayView<FVector3d> Chain)
	{
		const int32 Num = Chain.Num();
		if (Num <= 1) { return 0; }

		// Cost(K) = sum over t <= K of |A - C_t| + sum over t >= K of |B - C_t|.
		TArray<double> FromB;
		FromB.SetNumUninitialized(Num + 1);
		FromB[Num] = 0.0;
		for (int32 Index = Num - 1; Index >= 0; --Index)
		{
			FromB[Index] = FromB[Index + 1] + FVector3d::Distance(B, Chain[Index]);
		}
		int32 Best = 0;
		double BestCost = TNumericLimits<double>::Max();
		double FromA = 0.0;
		for (int32 K = 0; K < Num; ++K)
		{
			FromA += FVector3d::Distance(A, Chain[K]);
			const double Cost = FromA + FromB[K];
			if (Cost < BestCost)
			{
				BestCost = Cost;
				Best = K;
			}
		}
		return Best;
	}
}
