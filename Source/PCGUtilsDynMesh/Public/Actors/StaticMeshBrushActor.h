// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Actors/DynMeshBrushActor.h"
#include "StaticMeshBrushActor.generated.h"

class UStaticMeshBrushComponent;

UCLASS()
class PCGUTILSDYNMESH_API AStaticMeshBrushActor : public ADynMeshBrushActor
{
	GENERATED_BODY()

public:
	AStaticMeshBrushActor(const FObjectInitializer& ObjectInitializer);

	virtual UDynamicMesh* RetrieveDynamicMesh_Implementation() const override;

	UFUNCTION(BlueprintCallable, Category="Brush")
	UStaticMeshBrushComponent* GetStaticMeshBrushComponent() const { return StaticMeshBrushComponent; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Brush", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshBrushComponent> StaticMeshBrushComponent;
};
