#include "Interfaces/PCGPathProvider.h"

TArray<FPCGPathPoint> IPCGPathProvider::GetPCGPathPoints_Implementation(
	bool& bIsLocalSpace,
	bool& bIsLinearPath) const
{
	bIsLinearPath = true;

	TArray<FPCGPathPoint> PathPoints;
	const TArray<FPCGPoint> LegacyPoints = Execute_GetPathPoints(Cast<UObject>(this), bIsLocalSpace);
	PathPoints.Reserve(LegacyPoints.Num());
	for (const FPCGPoint& LegacyPoint : LegacyPoints)
	{
		PathPoints.Emplace(LegacyPoint);
	}

	return PathPoints;
}
