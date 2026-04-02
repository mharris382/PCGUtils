#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "OverrideGraphs.h"
#include "PCGSplineComponent.generated.h"

class UPCGGraphInterface;

UENUM(BlueprintType)
enum class EPCGSplineSnapMode : uint8
{
    None,
    Start,
    End,
    StartAndEnd,
    AllPoints
};

UENUM(BlueprintType)
enum class EPCGSplineSnapTargetMode : uint8
{
    ActorReferences,
    ComponentReferences,
    Siblings,
    OverlappingWorldActors
};

//This is a copy the FSplinePoint data struct used stored inside  
USTRUCT(BlueprintType)
struct FSnappedSplinePoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	float InputKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	FVector Position;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	FVector ArriveTangent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	FVector LeaveTangent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	FVector Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SplinePoint)
	TEnumAsByte<ESplinePointType::Type> Type;

	/** Default constructor */
	FSnappedSplinePoint()
		: InputKey(0.0f), Position(0.0f), ArriveTangent(0.0f), LeaveTangent(0.0f), Rotation(0.0f), Scale(1.0f), Type(ESplinePointType::Curve)
	{}
	FSnappedSplinePoint(const FSplinePoint& splinePoint)
		: InputKey(splinePoint.InputKey), Position(splinePoint.Position), ArriveTangent(splinePoint.ArriveTangent), LeaveTangent(splinePoint.LeaveTangent), Rotation(splinePoint.Rotation), Scale(splinePoint.Scale), Type(splinePoint.Type)
	{}
	

	/** Constructor taking a point position */
	FSnappedSplinePoint(float InInputKey, const FVector& InPosition)
		: InputKey(InInputKey), Position(InPosition), ArriveTangent(0.0f), LeaveTangent(0.0f), Rotation(0.0f), Scale(1.0f), Type(ESplinePointType::Curve)
	{}

	/** Constructor taking a point position and type, and optionally rotation and scale */
	FSnappedSplinePoint(float InInputKey, const FVector& InPosition, ESplinePointType::Type InType, const FRotator& InRotation = FRotator(0.0f), const FVector& InScale = FVector(1.0f))
		: InputKey(InInputKey), Position(InPosition), ArriveTangent(0.0f), LeaveTangent(0.0f), Rotation(InRotation), Scale(InScale), Type(InType)
	{}

	/** Constructor taking a point position and tangent, and optionally rotation and scale */
	FSnappedSplinePoint(float InInputKey,
				 const FVector& InPosition,
				 const FVector& InArriveTangent,
				 const FVector& InLeaveTangent,
				 const FRotator& InRotation = FRotator(0.0f),
				 const FVector& InScale = FVector(1.0f),
				 ESplinePointType::Type InType = ESplinePointType::CurveCustomTangent)
		: InputKey(InInputKey), Position(InPosition), ArriveTangent(InArriveTangent), LeaveTangent(InLeaveTangent), Rotation(InRotation), Scale(InScale), Type(InType)
	{}
};

UCLASS(ClassGroup = "PCG", meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class PCGUTILS_API UPCGSplineComponent : public USplineComponent,
                                          public IPCGOverrideGraphProvider
{
    GENERATED_BODY()

public:
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	UFUNCTION(BlueprintNativeEvent, Category="CourseSpline|Editor")
	void GetSplineEditorColors(FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;
	void GetSplineEditorColors_Implementation(FLinearColor& UnselectedColor, FLinearColor& SelectedColor, FLinearColor& TangentColor) const;
	
    UPCGSplineComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Spline")
	void OnUpdatedSpline();
	void OnUpdatedSpline_Implementation() { }

    
    virtual TArray<FPCGNamedOverrideGraph> GetOverrideGraphEntries_Implementation() const override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    float Height = 0.0f;

    // -------------------------------------------------------------------------
// PCG|Bake overrides
// -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PreBakeGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PostBakeGraph;

    // -------------------------------------------------------------------------
    // PCG|Spline overrides
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PreProcessSplineGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides")
    FPCGOverrideGraph PostSpawnGraph;
	

	
	
public:
	
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap")
    EPCGSplineSnapMode SnapMode = EPCGSplineSnapMode::None;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
    EPCGSplineSnapTargetMode SnapTargetMode = EPCGSplineSnapTargetMode::ComponentReferences;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
	//EPCGSplineSnappingLocationBehavior SnappingBehavior = EPCGSplineSnappingLocationBehavior::Location;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
	FVector SnappingOffset;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
	bool bIsAbsoluteOffset;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(
		ToolTip = "Acceptable difference between the snapped point and the original spline point.  If 0, the location will be snapped precisely", AllowPreserveRatio,
		EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
	FVector SnappingSoftRadius;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides))
	bool bIsSoftRadiusAbsolute;

	/** Distance in cm between samples when searching for the nearest point on a target spline. Smaller = more accurate but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None", EditConditionHides, ClampMin="1.0"))
	float SnapSampleInterval = 25.f;
	
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None && SnapTargetMode == EPCGSplineSnapTargetMode::OverlappingWorldActors ", EditConditionHides))
    FString SnapTargetActorTag;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta=(EditCondition="SnapMode != EPCGSplineSnapMode::None && (SnapTargetMode == EPCGSplineSnapTargetMode::Siblings || SnapTargetMode == EPCGSplineSnapTargetMode::OverlappingWorldActors || SnapTargetMode == EPCGSplineSnapTargetMode::ActorReferences) ", EditConditionHides))
	FString SnapTargetComponentTag;
	
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta = (EditCondition="SnapMode != EPCGSplineSnapMode::None && SnapTargetMode == EPCGSplineSnapTargetMode::ComponentReferences", EditConditionHides))
    TArray<TObjectPtr<USplineComponent>> SnapTargetComponents;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Snap", meta = (EditCondition="SnapMode != EPCGSplineSnapMode::None && SnapTargetMode == EPCGSplineSnapTargetMode::ActorReferences", EditConditionHides))
    TArray<TObjectPtr<AActor>> SnapTargetActors;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "PCG|Snap", AdvancedDisplay)
	TArray<FSnappedSplinePoint> SnappedSplinePoints;
    
    TArray<USplineComponent*> GetSnapTargets() const;
	
	UFUNCTION(BlueprintCallable, Category = "PCG|Spline")
	void RefreshSnappedSplinePoints();
	
		
	FSplinePoint FindSnappedSplinePointFor(const int splinePointIndex, const TArray<USplineComponent*> targets) const;
	
	UFUNCTION(BlueprintNativeEvent, CallInEditor, Category = "PCG|Snap")
	FSplinePoint PostProcessSnappedSplinePoint(const int splinePointIndex, const FSplinePoint& SnappedSplinePoint, const USplineComponent* SnappedSpline, float SnappedTime) const;
	FSplinePoint PostProcessSnappedSplinePoint_Implementation(const int splinePointIndex, const FSplinePoint& SnappedSplinePoint, const USplineComponent* SnappedSpline, float SnappedTime) const { return SnappedSplinePoint; }
	// -------------------------------------------------------------------------
	
private:
	FSplinePoint GetSnappedSplinePoint(const int splinePointIndex, const FSplinePoint& SplinePoint, const TArray<USplineComponent*>& TargetSplines) const;


    // PCG|Bake overrides
    // -------------------------------------------------------------------------

   //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (InlineEditConditionToggle))
   //bool bUsePreBakeGraph = false;
   //
   //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (EditCondition = "bUsePreBakeGraph", Tooltip = "Executes directly before baking. Output data will be baked."))
   //TObjectPtr<UPCGGraphInterface> PreBakeGraph;
	
/*    // -------------------------------------------------------------------------
    // PCG|Spline overrides
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides",meta = (InlineEditConditionToggle))
    bool bUsePreProcessSplineGraph = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", meta = (EditCondition = "bUsePreProcessSplineGraph", Tooltip = "Executes before spline data is processed. Allows modification of spline input data."))
    TObjectPtr<UPCGGraphInterface> PreProcessSplineGraph;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", AdvancedDisplay, meta = (InlineEditConditionToggle))
    bool bUseSplineProcessorStack = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Overrides", AdvancedDisplay, meta = (EditCondition = "bUseSplineProcessorStack"))
    TArray<FPCGOverrideGraph> SplineProcessorStack;
*/

};