// Copyright Max Harris

#include "Geometry/PCGUtilsProjectionSolver.h"

namespace PCGUtilsProjectionSolver
{
	FSolveResult Solve(const TConstArrayView<FSupportSample> InSamples, const FSolveSettings& InSettings)
	{
		FSolveResult Result;

		double MinTravel = TNumericLimits<double>::Max();
		double MaxTravel = TNumericLimits<double>::Lowest();

		for (const FSupportSample& Sample : InSamples)
		{
			if (!Sample.bHit)
			{
				continue;
			}

			++Result.NumHits;
			MinTravel = FMath::Min(MinTravel, Sample.Travel);
			MaxTravel = FMath::Max(MaxTravel, Sample.Travel);
		}

		if (Result.NumHits == 0)
		{
			return Result;
		}

		// The first sample to land stops the body, so the minimum is the answer. See the header: taking the
		// maximum instead is the one sign error in this file that would still look plausible in review, and it
		// sinks a slab until its highest corner touches.
		Result.Travel = MinTravel;
		Result.SupportSpread = MaxTravel - MinTravel;
		Result.Delta = FTransform(InSettings.Direction * MinTravel);
		Result.bSolved = true;
		return Result;
	}
}
