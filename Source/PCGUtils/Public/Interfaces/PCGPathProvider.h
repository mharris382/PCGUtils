#pragma once

#include "CoreMinimal.h"
#include "Data/PCGPathPoint.h"
#include "Data/PCGUtilsComponentData.h"
#include "PCGPoint.h"
#include "UObject/Interface.h"

#include "PCGPathProvider.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class PCGUTILS_API UPCGPathProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies a PCG point path, its path properties, and shared path data. */
class PCGUTILS_API IPCGPathProvider
{
	GENERATED_BODY()

public:
	/** Supplies path-specific points. Non-linear paths use ArriveTangent and LeaveTangent. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	TArray<FPCGPathPoint> GetPCGPathPoints(bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	FPathComponentData GetPathData() const;

	/**
	 * How many separate paths this provider supplies, for providers with several (an Image Path's contours).
	 * 0, the default, means one path, read through GetPCGPathPoints.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	int32 GetPCGPathCount() const;

	/** One of the GetPCGPathCount paths. bIsHole marks a path bounding a hole inside another. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PCG|Path")
	TArray<FPCGPathPoint> GetPCGPathPointsAt(int32 PathIndex, bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop, bool& bIsHole) const;

	virtual int32 GetPCGPathCount_Implementation() const { return 0; }
	virtual TArray<FPCGPathPoint> GetPCGPathPointsAt_Implementation(
		int32 PathIndex, bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop, bool& bIsHole) const;
};
