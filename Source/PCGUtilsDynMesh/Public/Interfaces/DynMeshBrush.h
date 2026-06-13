// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Types/DynMeshBrushTypes.h"
#include "UObject/Interface.h"
#include "DynMeshBrush.generated.h"

class UDynamicMesh;

UINTERFACE(BlueprintType)
class PCGUTILSDYNMESH_API UDynMeshBrush : public UInterface
{
	GENERATED_BODY()
};

class PCGUTILSDYNMESH_API IDynMeshBrush
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="DynMesh Brush")
	EPCGUtilsDynMeshBrushType GetDynMeshBrushType() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="DynMesh Brush")
	FDynMeshBrushSettings GetDynMeshBrushSettings() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="DynMesh Brush")
	UDynamicMesh* RetrieveDynamicMesh() const;
};
