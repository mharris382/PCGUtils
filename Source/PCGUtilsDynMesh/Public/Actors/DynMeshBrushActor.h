// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/DynMeshBrush.h"
#include "GameFramework/Actor.h"
#include "Types/DynMeshBrushTypes.h"
#include "DynMeshBrushActor.generated.h"

class UDynamicMesh;

UCLASS(Abstract, Blueprintable)
class PCGUTILSDYNMESH_API ADynMeshBrushActor : public AActor, public IDynMeshBrush
{
	GENERATED_BODY()

public:
	ADynMeshBrushActor(const FObjectInitializer& ObjectInitializer);

	virtual EPCGUtilsDynMeshBrushType GetDynMeshBrushType_Implementation() const override;
	virtual FDynMeshBrushSettings GetDynMeshBrushSettings_Implementation() const override;
	virtual UDynamicMesh* RetrieveDynamicMesh_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush")
	EPCGUtilsDynMeshBrushType BrushType = EPCGUtilsDynMeshBrushType::Subtract;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush")
	FDynMeshBrushSettings BrushSettings;
};
