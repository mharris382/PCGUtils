#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/PCGUtilsComponentData.h"
#include "Interfaces/PCGBoundsProvider.h"
#include "Interfaces/PCGPathProvider.h"
#include "OverrideGraphs.h"
#include "ShapePath/ShapePathGenerator.h"
#include "ShapePathComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UShapePathComponent : public USceneComponent, public IPCGPathProvider, public IPCGBoundsProvider
{
	GENERATED_BODY()
public:
	UShapePathComponent();
	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;

	UPROPERTY(EditAnywhere, Instanced, Category="PCG",meta  = (DisplayPriority=0))
	TObjectPtr<UShapePathGenerator> Generator;

	/** Standardized path data used by PCG path getter elements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG", meta = (DisplayPriority=1, ShowOnlyInnerProperties))
	FPathComponentData PathData;

	// Legacy fields are intentionally retained for asset migration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Depricated")
	FPCGOverrideGraph PreProcessShapePath;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Depricated")
	float PathHeight = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Depricated", meta = (ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Depricated", meta = (InlineEditConditionToggle))
	bool bSetPathDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Depricated", meta = (EditCondition="bSetPathDensity", UIMin=0, UIMax=1))
	float PathDensity = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Depricated", meta = (InlineEditConditionToggle))
	bool bSetPathColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Depricated", meta = (EditCondition="bSetPathColor"))
	FLinearColor PathColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
	FLinearColor GetPathColor() const	{ return bSetPathColor ? PathColor : FLinearColor::White; }
	float GetPathDensity() const { return  bSetPathDensity ? PathDensity : 1.0f; }
	
	virtual TArray<FPCGPoint> GetPathPoints_Implementation(bool& bIsLocalSpace) const override;
	virtual FPathComponentData GetPathData_Implementation() const override { return PathData; }
	virtual bool GetIsClosedLoop_Implementation() const override;
	const TArray<FVector>& GetGeneratedPathPoints();
	int32 GetNumPoints() const;
	bool IsClosedLoop() const;
	virtual bool GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const override;
	
	
	FVector GetPathPoint(int32 PointIndex) const;

	/** Cached output — public so the visualizer can access it directly. */
	UPROPERTY(Transient)
	TArray<FVector> CachedPoints;

	
	
	void RebuildPoints();

protected:
	virtual void OnRegister() override;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Path" ,AdvancedDisplay)
	bool bAllowRegeneratePCGOnEdits = true;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	bool bPathDataMigrated = false;
};
