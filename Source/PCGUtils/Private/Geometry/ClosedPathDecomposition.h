#pragma once
#include "CoreMinimal.h"

namespace ClosedPathDecomposition
{
	struct FOptions
	{
		double AreaWeight = 1, LengthWeight = .1;
		double Concavity = .1, QuadWarp = .35, PolygonWarp = .02;
		int32 MaxPoints = 2048;
		bool bBiasFlatCuts = false;
	};
	using FLoop = TArray<int32>;
	// Pure geometry. Coordinates must share one metric frame; outputs index the input.
	bool Solve(const TArray<FVector>& Points, const FOptions& Options, TArray<FLoop>& OutLoops, FString& Error);
}
