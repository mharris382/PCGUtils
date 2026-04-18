#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interfaces/PathProvider.h"
#include "OverrideGraphs.h"
#include "ShapePath/ShapePathGenerator.h"
#include "ShapePathComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UShapePathComponent : public USceneComponent, public IPathProvider
{
	GENERATED_BODY()
public:
	UShapePathComponent();

	UPROPERTY(EditAnywhere, Instanced, Category="Shape Path")
	TObjectPtr<UShapePathGenerator> Generator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape Path|PCG")
	FPCGOverrideGraph PreProcessShapePath;

	// IPathProvider
	virtual const TArray<FVector>& GetPathPoints() override;
	virtual int32 GetNumPoints() const override;
	virtual bool GetIsClosedLoop() const override;
	virtual FTransform GetPathTransform() const override;

	/** Cached output — public so the visualizer can access it directly. */
	UPROPERTY(Transient)
	TArray<FVector> CachedPoints;

	void RebuildPoints();

protected:
	virtual void OnRegister() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
};
