// Copyright Max Harris

#include "Vectorization/PCGUtilsVectorization.h"
#include "Vectorization/PCGUtilsVectorizationInternal.h"

#include "Algo/Reverse.h"

namespace PCGUtilsVectorization
{
	double SignedArea(TConstArrayView<FVector2D> Points)
	{
		double Twice = 0.0;
		const int32 Num = Points.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2D& A = Points[Index];
			const FVector2D& B = Points[(Index + 1) % Num];
			Twice += A.X * B.Y - B.X * A.Y;
		}
		return Twice * 0.5;
	}

	namespace
	{
		double DistanceToSegmentSquared(const FVector2D& P, const FVector2D& A, const FVector2D& B)
		{
			const FVector2D AB = B - A;
			const double LengthSq = AB.SizeSquared();
			const double T = LengthSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
			return FVector2D::DistSquared(P, A + AB * T);
		}

		/** Marks the points of an open polyline Douglas-Peucker keeps, endpoints always included. */
		void DouglasPeuckerKeep(TConstArrayView<FVector2D> Points, double EpsilonSq, TBitArray<>& OutKeep)
		{
			OutKeep.Init(false, Points.Num());
			if (Points.Num() < 3)
			{
				OutKeep.Init(true, Points.Num());
				return;
			}
			OutKeep[0] = true;
			OutKeep[Points.Num() - 1] = true;
			TArray<TPair<int32, int32>> Stack;
			Stack.Emplace(0, Points.Num() - 1);
			while (!Stack.IsEmpty())
			{
				const TPair<int32, int32> Range = Stack.Pop(EAllowShrinking::No);
				double FarthestSq = -1.0;
				int32 Farthest = INDEX_NONE;
				for (int32 Index = Range.Key + 1; Index < Range.Value; ++Index)
				{
					const double DistanceSq = DistanceToSegmentSquared(Points[Index], Points[Range.Key], Points[Range.Value]);
					if (DistanceSq > FarthestSq)
					{
						FarthestSq = DistanceSq;
						Farthest = Index;
					}
				}
				if (Farthest != INDEX_NONE && FarthestSq > EpsilonSq)
				{
					OutKeep[Farthest] = true;
					Stack.Emplace(Range.Key, Farthest);
					Stack.Emplace(Farthest, Range.Value);
				}
			}
		}

		/** Even-odd ray cast. Points exactly on the boundary may go either way. */
		bool PointInPolygon(const FVector2D& P, TConstArrayView<FVector2D> Polygon)
		{
			bool bInside = false;
			const int32 Num = Polygon.Num();
			for (int32 I = 0, J = Num - 1; I < Num; J = I++)
			{
				const FVector2D& A = Polygon[I];
				const FVector2D& B = Polygon[J];
				if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
				{
					bInside = !bInside;
				}
			}
			return bInside;
		}

		/** True when most of Inner's sampled vertices lie inside Outer; robust to the odd vertex on Outer's boundary. */
		bool PolygonInsidePolygon(TConstArrayView<FVector2D> Inner, TConstArrayView<FVector2D> Outer, const FBox2D& OuterBounds)
		{
			const int32 Samples = FMath::Min(Inner.Num(), 9);
			int32 Votes = 0;
			for (int32 Sample = 0; Sample < Samples; ++Sample)
			{
				const FVector2D& P = Inner[Sample * Inner.Num() / Samples];
				if (OuterBounds.IsInsideOrOn(P) && PointInPolygon(P, Outer))
				{
					++Votes;
				}
			}
			return Votes * 2 > Samples;
		}

		void ReversePath(FPCGUtilsVectorPath& Path)
		{
			Algo::Reverse(Path.Points);
			if (Path.HasTangents())
			{
				// Walking backwards, what left a point now arrives at it, pointing the other way.
				TArray<FVector2D> NewArrive = Path.LeaveTangents;
				TArray<FVector2D> NewLeave = Path.ArriveTangents;
				Algo::Reverse(NewArrive);
				Algo::Reverse(NewLeave);
				for (FVector2D& Tangent : NewArrive) { Tangent = -Tangent; }
				for (FVector2D& Tangent : NewLeave) { Tangent = -Tangent; }
				Path.ArriveTangents = MoveTemp(NewArrive);
				Path.LeaveTangents = MoveTemp(NewLeave);
			}
		}
	}

	void SimplifyDouglasPeucker(TArray<FVector2D>& Points, double Epsilon, bool bClosed)
	{
		if (Epsilon <= 0.0 || Points.Num() < (bClosed ? 4 : 3))
		{
			return;
		}
		const double EpsilonSq = Epsilon * Epsilon;
		if (!bClosed)
		{
			TBitArray<> Keep;
			DouglasPeuckerKeep(Points, EpsilonSq, Keep);
			TArray<FVector2D> Kept;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				if (Keep[Index]) { Kept.Add(Points[Index]); }
			}
			Points = MoveTemp(Kept);
			return;
		}

		// A closed loop has no endpoints of its own: anchor it at point 0 and the point farthest from it.
		int32 Farthest = 0;
		double FarthestSq = -1.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const double DistanceSq = FVector2D::DistSquared(Points[0], Points[Index]);
			if (DistanceSq > FarthestSq) { FarthestSq = DistanceSq; Farthest = Index; }
		}
		TArray<FVector2D> First(Points.GetData(), Farthest + 1);
		TArray<FVector2D> Second(Points.GetData() + Farthest, Points.Num() - Farthest);
		Second.Add(Points[0]);
		TBitArray<> KeepFirst, KeepSecond;
		DouglasPeuckerKeep(First, EpsilonSq, KeepFirst);
		DouglasPeuckerKeep(Second, EpsilonSq, KeepSecond);
		TArray<FVector2D> Kept;
		for (int32 Index = 0; Index < First.Num(); ++Index)
		{
			if (KeepFirst[Index]) { Kept.Add(First[Index]); }
		}
		// Second starts at the shared farthest point and ends at point 0, both already kept.
		for (int32 Index = 1; Index < Second.Num() - 1; ++Index)
		{
			if (KeepSecond[Index]) { Kept.Add(Second[Index]); }
		}
		Points = MoveTemp(Kept);
	}

	void FinalizePaths(TArray<FPCGUtilsVectorPath>& Paths, double MinArea, EPCGUtilsContourRetrieval Retrieval)
	{
		Internal::FinalizePaths(Paths, MinArea, Retrieval, /*bHierarchyKnown=*/false);
	}

	namespace Internal
	{
		void FinalizePaths(TArray<FPCGUtilsVectorPath>& Paths, double MinArea, EPCGUtilsContourRetrieval Retrieval, bool bHierarchyKnown)
		{
			const int32 Num = Paths.Num();
			TArray<double> Signed;
			Signed.SetNumUninitialized(Num);
			TBitArray<> Keep(false, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Signed[Index] = Paths[Index].Points.Num() >= 3 ? SignedArea(Paths[Index].Points) : 0.0;
				Paths[Index].Area = static_cast<float>(FMath::Abs(Signed[Index]));
				Keep[Index] = Paths[Index].Points.Num() >= 3 && FMath::Abs(Signed[Index]) >= MinArea && FMath::Abs(Signed[Index]) > 0.0;
			}

			// Parents among kept paths only: a dropped speckle hands its children to its own parent.
			TArray<int32> Parent;
			Parent.Init(INDEX_NONE, Num);
			if (bHierarchyKnown)
			{
				for (int32 Index = 0; Index < Num; ++Index)
				{
					int32 Candidate = Paths[Index].ParentIndex;
					while (Candidate != INDEX_NONE && !Keep[Candidate])
					{
						Candidate = Paths[Candidate].ParentIndex;
					}
					Parent[Index] = Candidate;
				}
			}
			else
			{
				// Larger paths first, so the first containing path found walking back is the smallest one.
				TArray<int32> BySize;
				for (int32 Index = 0; Index < Num; ++Index)
				{
					if (Keep[Index]) { BySize.Add(Index); }
				}
				BySize.Sort([&Paths](int32 A, int32 B) { return Paths[A].Area > Paths[B].Area; });
				TArray<FBox2D> Bounds;
				Bounds.SetNum(Num);
				for (const int32 Index : BySize)
				{
					Bounds[Index] = FBox2D(Paths[Index].Points);
				}
				for (int32 Rank = 1; Rank < BySize.Num(); ++Rank)
				{
					const int32 Inner = BySize[Rank];
					for (int32 OuterRank = Rank - 1; OuterRank >= 0; --OuterRank)
					{
						const int32 Outer = BySize[OuterRank];
						if (Bounds[Outer].IsInside(Bounds[Inner].GetCenter())
							&& PolygonInsidePolygon(Paths[Inner].Points, Paths[Outer].Points, Bounds[Outer]))
						{
							Parent[Inner] = Outer;
							break;
						}
					}
				}
			}

			// Depth by walking parents (parents are always shallower, so memoize as we go).
			TArray<int32> Depth;
			Depth.Init(INDEX_NONE, Num);
			TFunction<int32(int32)> ResolveDepth = [&](int32 Index) -> int32
			{
				if (Depth[Index] == INDEX_NONE)
				{
					Depth[Index] = Parent[Index] == INDEX_NONE ? 0 : ResolveDepth(Parent[Index]) + 1;
				}
				return Depth[Index];
			};

			TArray<int32> Order;
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (Keep[Index] && (Retrieval == EPCGUtilsContourRetrieval::All || ResolveDepth(Index) == 0))
				{
					Order.Add(Index);
				}
			}
			Order.StableSort([&](int32 A, int32 B)
			{
				const int32 DepthA = ResolveDepth(A), DepthB = ResolveDepth(B);
				return DepthA != DepthB ? DepthA < DepthB : Paths[A].Area > Paths[B].Area;
			});

			TArray<int32> NewIndex;
			NewIndex.Init(INDEX_NONE, Num);
			for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
			{
				NewIndex[Order[Rank]] = Rank;
			}

			TArray<FPCGUtilsVectorPath> Result;
			Result.Reserve(Order.Num());
			for (const int32 Index : Order)
			{
				FPCGUtilsVectorPath& Path = Result.Add_GetRef(MoveTemp(Paths[Index]));
				Path.Depth = ResolveDepth(Index);
				Path.bIsHole = (Path.Depth % 2) == 1;
				Path.ParentIndex = Parent[Index] != INDEX_NONE ? NewIndex[Parent[Index]] : INDEX_NONE;
				Path.bClosed = true;
				// Outer boundaries positive, holes negative.
				if ((Signed[Index] > 0.0) == Path.bIsHole)
				{
					ReversePath(Path);
				}
			}
			Paths = MoveTemp(Result);
		}
	}
}
