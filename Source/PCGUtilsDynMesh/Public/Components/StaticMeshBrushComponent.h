// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshBrushComponent.generated.h"

UCLASS(ClassGroup=(PCGUtils), meta=(BlueprintSpawnableComponent))
class PCGUTILSDYNMESH_API UStaticMeshBrushComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshBrushComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ApplyBrushDefaults();
};
