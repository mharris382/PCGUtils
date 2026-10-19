// Copyright Max Harris

#include "Interfaces/PCGPathProvider.h"

TArray<FPCGPathPoint> IPCGPathProvider::GetPCGPathPointsAt_Implementation(
	int32 PathIndex, bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop, bool& bIsHole) const
{
	// A single-path provider: its one path is index 0, and it has no holes.
	bIsHole = false;
	if (PathIndex != 0)
	{
		bIsLocalSpace = false;
		bIsLinearPath = true;
		bIsClosedLoop = false;
		return {};
	}
	return Execute_GetPCGPathPoints(_getUObject(), bIsLocalSpace, bIsLinearPath, bIsClosedLoop);
}
