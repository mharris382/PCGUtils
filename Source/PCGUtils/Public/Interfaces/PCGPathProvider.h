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
};
