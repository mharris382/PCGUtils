// Copyright Max Harris
// Least-squares cubic Bezier fitting with Newton-Raphson reparameterization: P. J. Schneider, "An Algorithm for
// Automatically Fitting Digitized Curves", Graphics Gems (1990). Run on marching-squares iso-contours, split at
// corners first so sharp turns survive.

#include "Vectorization/PCGUtilsVectorization.h"
#include "Vectorization/PCGUtilsVectorizationInternal.h"

namespace PCGUtilsVectorization
{
	namespace
	{
		struct FCurveFitBezier
		{
			FVector2D P[4];

			FVector2D Evaluate(double T) const
			{
				const double U = 1.0 - T;
				return P[0] * (U * U * U) + P[1] * (3.0 * U * U * T) + P[2] * (3.0 * U * T * T) + P[3] * (T * T * T);
			}
			FVector2D Derivative(double T) const
			{
				const double U = 1.0 - T;
				return (P[1] - P[0]) * (3.0 * U * U) + (P[2] - P[1]) * (6.0 * U * T) + (P[3] - P[2]) * (3.0 * T * T);
			}
			FVector2D SecondDerivative(double T) const
			{
				return (P[2] - P[1] * 2.0 + P[0]) * (6.0 * (1.0 - T)) + (P[3] - P[2] * 2.0 + P[1]) * (6.0 * T);
			}
		};

		class FCurveFitter
		{
		public:
			FCurveFitter(TConstArrayView<FVector2D> InPoints, double InErrorSq, TArray<FCurveFitBezier>& InOut)
				: D(InPoints), ErrorSq(InErrorSq), Out(InOut)
			{
			}

			void Fit(int32 First, int32 Last, FVector2D THat1, FVector2D THat2)
			{
				const int32 Count = Last - First + 1;
				if (Count == 2)
				{
					const double Distance = FVector2D::Distance(D[First], D[Last]) / 3.0;
					Out.Add({{D[First], D[First] + THat1 * Distance, D[Last] + THat2 * Distance, D[Last]}});
					return;
				}

				TArray<double> U = ChordLengthParameterize(First, Last);
				FCurveFitBezier Bezier = Generate(First, Last, U, THat1, THat2);
				int32 Split = 0;
				double MaxError = ComputeMaxError(First, Last, Bezier, U, Split);
				if (MaxError < ErrorSq)
				{
					Out.Add(Bezier);
					return;
				}

				// Close enough to be worth improving the parameterization rather than splitting straight away.
				if (MaxError < ErrorSq * 4.0)
				{
					for (int32 Iteration = 0; Iteration < 4; ++Iteration)
					{
						U = Reparameterize(First, Last, U, Bezier);
						Bezier = Generate(First, Last, U, THat1, THat2);
						MaxError = ComputeMaxError(First, Last, Bezier, U, Split);
						if (MaxError < ErrorSq)
						{
							Out.Add(Bezier);
							return;
						}
					}
				}

				const FVector2D Center = (D[Split - 1] - D[Split + 1]).GetSafeNormal();
				Fit(First, Split, THat1, Center);
				Fit(Split, Last, -Center, THat2);
			}

		private:
			TArray<double> ChordLengthParameterize(int32 First, int32 Last) const
			{
				TArray<double> U;
				U.SetNumUninitialized(Last - First + 1);
				U[0] = 0.0;
				for (int32 Index = First + 1; Index <= Last; ++Index)
				{
					U[Index - First] = U[Index - First - 1] + FVector2D::Distance(D[Index], D[Index - 1]);
				}
				const double Total = U.Last();
				for (double& Value : U)
				{
					Value = Total > 0.0 ? Value / Total : 0.0;
				}
				return U;
			}

			FCurveFitBezier Generate(int32 First, int32 Last, TConstArrayView<double> U, const FVector2D& THat1, const FVector2D& THat2) const
			{
				const FVector2D& D0 = D[First];
				const FVector2D& D3 = D[Last];
				double C00 = 0, C01 = 0, C11 = 0, X0 = 0, X1 = 0;
				for (int32 Index = 0; Index < U.Num(); ++Index)
				{
					const double T = U[Index], V = 1.0 - T;
					const double B0 = V * V * V, B1 = 3.0 * V * V * T, B2 = 3.0 * V * T * T, B3 = T * T * T;
					const FVector2D A0 = THat1 * B1;
					const FVector2D A1 = THat2 * B2;
					C00 += FVector2D::DotProduct(A0, A0);
					C01 += FVector2D::DotProduct(A0, A1);
					C11 += FVector2D::DotProduct(A1, A1);
					const FVector2D Residual = D[First + Index] - (D0 * (B0 + B1) + D3 * (B2 + B3));
					X0 += FVector2D::DotProduct(A0, Residual);
					X1 += FVector2D::DotProduct(A1, Residual);
				}
				const double DetC = C00 * C11 - C01 * C01;
				double AlphaL = DetC != 0.0 ? (X0 * C11 - X1 * C01) / DetC : 0.0;
				double AlphaR = DetC != 0.0 ? (C00 * X1 - C01 * X0) / DetC : 0.0;

				// A degenerate or backwards solution falls back to Wu/Barsky's heuristic: handles a third of the chord.
				const double SegmentLength = FVector2D::Distance(D0, D3);
				const double Epsilon = 1.0e-6 * SegmentLength;
				if (AlphaL < Epsilon || AlphaR < Epsilon)
				{
					AlphaL = AlphaR = SegmentLength / 3.0;
				}
				return {{D0, D0 + THat1 * AlphaL, D3 + THat2 * AlphaR, D3}};
			}

			double ComputeMaxError(int32 First, int32 Last, const FCurveFitBezier& Bezier, TConstArrayView<double> U, int32& OutSplit) const
			{
				OutSplit = (First + Last) / 2;
				double MaxDistanceSq = 0.0;
				for (int32 Index = First + 1; Index < Last; ++Index)
				{
					const double DistanceSq = FVector2D::DistSquared(Bezier.Evaluate(U[Index - First]), D[Index]);
					if (DistanceSq >= MaxDistanceSq)
					{
						MaxDistanceSq = DistanceSq;
						OutSplit = Index;
					}
				}
				OutSplit = FMath::Clamp(OutSplit, First + 1, Last - 1);
				return MaxDistanceSq;
			}

			TArray<double> Reparameterize(int32 First, int32 Last, TConstArrayView<double> U, const FCurveFitBezier& Bezier) const
			{
				TArray<double> Result;
				Result.SetNumUninitialized(U.Num());
				for (int32 Index = 0; Index < U.Num(); ++Index)
				{
					const double T = U[Index];
					const FVector2D Delta = Bezier.Evaluate(T) - D[First + Index];
					const FVector2D Q1 = Bezier.Derivative(T);
					const FVector2D Q2 = Bezier.SecondDerivative(T);
					const double Denominator = FVector2D::DotProduct(Q1, Q1) + FVector2D::DotProduct(Delta, Q2);
					Result[Index] = FMath::IsNearlyZero(Denominator) ? T : FMath::Clamp(T - FVector2D::DotProduct(Delta, Q1) / Denominator, 0.0, 1.0);
				}
				return Result;
			}

			TConstArrayView<FVector2D> D;
			double ErrorSq;
			TArray<FCurveFitBezier>& Out;
		};

		/** Indices of the loop's corners: local maxima of the turning angle, measured over a few points either side. */
		TArray<int32> FindCurveFitCorners(TConstArrayView<FVector2D> Loop, double CornerAngleDegrees)
		{
			TArray<int32> Corners;
			const int32 Num = Loop.Num();
			if (CornerAngleDegrees >= 180.0 || Num < 8)
			{
				return Corners;
			}
			const int32 Window = 2;
			const double Threshold = FMath::DegreesToRadians(CornerAngleDegrees);
			TArray<double> Turn;
			Turn.SetNumZeroed(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FVector2D In = (Loop[Index] - Loop[(Index - Window + Num) % Num]).GetSafeNormal();
				const FVector2D Out = (Loop[(Index + Window) % Num] - Loop[Index]).GetSafeNormal();
				Turn[Index] = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In, Out), -1.0, 1.0));
			}
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (Turn[Index] < Threshold)
				{
					continue;
				}
				bool bPeak = true;
				for (int32 Offset = -Window; Offset <= Window && bPeak; ++Offset)
				{
					const int32 Other = (Index + Offset + Num) % Num;
					// Ties go to the earlier index, so a flat-topped peak yields one corner.
					bPeak = Offset == 0 || Turn[Other] < Turn[Index] || (Turn[Other] == Turn[Index] && Offset > 0);
				}
				if (bPeak)
				{
					Corners.Add(Index);
				}
			}
			return Corners;
		}

		/** Fits one closed loop and writes it as points with Hermite tangents (a Bezier handle is P +- T / 3). */
		void FitCurveLoop(TConstArrayView<FVector2D> Loop, const FPCGUtilsCurveFitOptions& Options, FPCGUtilsVectorPath& OutPath)
		{
			const int32 Num = Loop.Num();
			TArray<int32> Breaks = FindCurveFitCorners(Loop, Options.CornerAngle);
			const bool bSmoothSeams = Breaks.IsEmpty();
			if (bSmoothSeams)
			{
				// No corners: break at two opposite points and keep the tangent continuous across both.
				Breaks = {0, Num / 2};
			}

			TArray<FCurveFitBezier> Segments;
			const double ErrorSq = FMath::Square(FMath::Max(Options.MaxError, 0.01f));
			for (int32 Piece = 0; Piece < Breaks.Num(); ++Piece)
			{
				const int32 Start = Breaks[Piece];
				const int32 End = Breaks[(Piece + 1) % Breaks.Num()];
				const int32 Length = ((End - Start + Num - 1) % Num) + 1; // points after Start up to and including End
				TArray<FVector2D> Run;
				Run.Reserve(Length + 1);
				for (int32 Step = 0; Step <= Length; ++Step)
				{
					Run.Add(Loop[(Start + Step) % Num]);
				}

				auto SeamTangent = [&](int32 At) { return (Loop[(At + 1) % Num] - Loop[(At - 1 + Num) % Num]).GetSafeNormal(); };
				const FVector2D THat1 = bSmoothSeams ? SeamTangent(Start) : (Run[FMath::Min(2, Run.Num() - 1)] - Run[0]).GetSafeNormal();
				const FVector2D THat2 = bSmoothSeams ? -SeamTangent(End) : (Run[FMath::Max(Run.Num() - 3, 0)] - Run.Last()).GetSafeNormal();
				FCurveFitter(Run, ErrorSq, Segments).Fit(0, Run.Num() - 1, THat1, THat2);
			}

			const int32 Count = Segments.Num();
			OutPath.Points.SetNum(Count);
			OutPath.ArriveTangents.SetNum(Count);
			OutPath.LeaveTangents.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FCurveFitBezier& Segment = Segments[Index];
				OutPath.Points[Index] = Segment.P[0];
				OutPath.LeaveTangents[Index] = (Segment.P[1] - Segment.P[0]) * 3.0;
				OutPath.ArriveTangents[(Index + 1) % Count] = (Segment.P[3] - Segment.P[2]) * 3.0;
			}
		}
	}

	void CurveFit(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsCurveFitOptions& Options, TArray<FPCGUtilsVectorPath>& OutPaths)
	{
		OutPaths.Reset();
		TArray<TArray<FVector2D>> Loops;
		Internal::TraceIsoContours(Image, Options.IsoLevel, /*bInterpolate=*/true, EPCGUtilsMarchingSquaresSaddle::Average, Loops);

		// Nest and filter the dense polylines first, then fit only the paths that are kept.
		OutPaths.Reserve(Loops.Num());
		for (TArray<FVector2D>& Loop : Loops)
		{
			OutPaths.AddDefaulted_GetRef().Points = MoveTemp(Loop);
		}
		Internal::FinalizePaths(OutPaths, Options.MinArea, Options.Retrieval, /*bHierarchyKnown=*/false);

		for (FPCGUtilsVectorPath& Path : OutPaths)
		{
			const TArray<FVector2D> Dense = MoveTemp(Path.Points);
			FitCurveLoop(Dense, Options, Path);
			// Fitting keeps the winding of the dense loop, so outer boundaries stay positive and holes negative.
		}
	}
}
