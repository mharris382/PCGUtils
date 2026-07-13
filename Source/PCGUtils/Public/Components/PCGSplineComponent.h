#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "OverrideGraphs.h"
#include "PCGSplineComponent.generated.h"

enum class ESplineLoopMode : uint8
{
	UserControlled,
	ClosedLoopOnly,
	OpenLoopOnly
};

UCLASS(ClassGroup = "PCG", meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class PCGUTILS_API UPCGSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UPCGSplineComponent(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintNativeEvent, Category = "CourseSpline|Editor")
	void GetSplineEditorColors(bool& bHasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;
	virtual void GetSplineEditorColors_Implementation(bool& bHasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;

	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Spline")
	void OnUpdatedSpline();
	void OnUpdatedSpline_Implementation() { }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	FPCGOverrideGraph PreProcessSplineGraph;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	float Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta = (ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPathDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPathDensity", UIMin=0, UIMax=1))
	float PathDensity = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (InlineEditConditionToggle))
	bool bSetPathColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG", meta = (EditCondition="bSetPathColor"))
	FLinearColor PathColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
#if WITH_EDITORONLY_DATA
			
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Editor")
	bool bRegeneratePCGOnSplineEdits = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Editor", AdvancedDisplay)
	bool bAutoSetSplineColors = false;
#endif

protected:
	virtual ESplineLoopMode GetSplineLoopMode() { return ESplineLoopMode::UserControlled; }
};
