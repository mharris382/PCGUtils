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

	UPROPERTY(EditAnywhere, Instanced, Category="PCG")
	TObjectPtr<UShapePathGenerator> Generator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG")
	FPCGOverrideGraph PreProcessShapePath;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG")
	float PathHeight = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;

	// IPathProvider
	virtual const TArray<FVector>& GetPathPoints() override;
	virtual int32 GetNumPoints() const override;
	virtual bool GetIsClosedLoop() const override;
	virtual FTransform GetPathTransform() const override;
	
	
	FVector GetPathPoint(int32 PointIndex) const;

	/** Cached output — public so the visualizer can access it directly. */
	UPROPERTY(Transient)
	TArray<FVector> CachedPoints;

	void RebuildPoints();

protected:
	virtual void OnRegister() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Path" ,AdvancedDisplay)
	bool bAllowRegeneratePCGOnEdits = true;
	
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
