#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PathProvider.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UPathProvider : public UInterface { GENERATED_BODY() };

class PCGUTILS_API IPathProvider
{
	GENERATED_BODY()
public:
	virtual const TArray<FVector>& GetPathPoints() = 0;
	virtual int32 GetNumPoints() const = 0;
	virtual bool GetIsClosedLoop() const = 0;
	virtual FTransform GetPathTransform() const = 0;
};
