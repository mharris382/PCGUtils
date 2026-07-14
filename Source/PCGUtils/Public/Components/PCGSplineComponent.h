#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Data/PathComponentData.h"
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


	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Spline")
	void OnUpdatedSpline();
	void OnUpdatedSpline_Implementation() { }

	/** Standardized path data used by PCG path getter elements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
	FPathComponentData PathData;

	/** Copies the retained legacy fields below into PathData. */
	UFUNCTION(CallInEditor, Category = "PCG|Migration", meta = (DisplayName = "Copy Legacy Path Data"))
	void CopyLegacyPathData();

	// Legacy fields are intentionally retained for asset migration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Deprecated", meta = (DepricatedProperty, DepricationMessage="UsePathData Version"))
	FPCGOverrideGraph PreProcessSplineGraph;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Deprecated", meta = (DepricatedProperty, DepricationMessage="UsePathData Version"))
	float Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Deprecated", meta = (ToolTip = "General Purpose path ID. intended use case is to make it easy to union grouped path into single path"))
	int32 GroupID = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Deprecated", meta = (InlineEditConditionToggle))
	bool bSetPathDensity = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Deprecated", meta = (EditCondition="bSetPathDensity", UIMin=0, UIMax=1))
	float PathDensity = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Deprecated", meta = (InlineEditConditionToggle))
	bool bSetPathColor = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "PCG|Deprecated", meta = (EditCondition="bSetPathColor"))
	FLinearColor PathColor = FLinearColor(1.0f, 1.0f, 1.0f);
	
#if WITH_EDITORONLY_DATA
			
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Editor")
	bool bRegeneratePCGOnSplineEdits = true;

#endif

	UFUNCTION(BlueprintNativeEvent, Category = "Spline|Editor")
	void GetSplineEditorColors(bool& bHasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;
	virtual void GetSplineEditorColors_Implementation(bool& bHasScriptedEditorColors, FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;

#if WITH_EDITOR
	
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
#endif
	
protected:
	virtual ESplineLoopMode GetSplineLoopMode() { return ESplineLoopMode::UserControlled; }
};
