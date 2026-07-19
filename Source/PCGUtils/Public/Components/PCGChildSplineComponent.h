#pragma once

#include "CoreMinimal.h"
#include "Components/PCGSplineComponent.h"
#include "PCGChildSplineComponent.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EPCGChildSplineEndpointTangentMode : uint8
{
	Automatic UMETA(DisplayName = "Automatic"),
	Linear UMETA(DisplayName = "Linear"),
	Free UMETA(DisplayName = "Free")
};

/**
 * A spline made from a normalized portion of the spline component to which it is attached.
 * Generated points are rebuilt from the parent while the user-owned points at either end are preserved.
 */
UCLASS(ClassGroup = "PCG", meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class PCGUTILS_API UPCGChildSplineComponent : public UPCGSplineComponent
{
	GENERATED_BODY()

public:
	UPCGChildSplineComponent(const FObjectInitializer& ObjectInitializer);

	/** Copy from a tagged spline on another actor instead of the attached parent spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline|Source")
	bool bCopyExternalSpline = false;

	/** Actor searched for the external source spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline|Source", meta = (EditCondition = "bCopyExternalSpline"))
	TObjectPtr<AActor> ExternalSplineActor = nullptr;

	/** The first spline component with this component tag is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline|Source", meta = (EditCondition = "bCopyExternalSpline"))
	FName ExternalSplineComponentTag = NAME_None;

	/** Normalized source time. Open sources clamp to [0,1]; closed sources wrap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	float CopySplineStartTime = 0.0f;

	/** Normalized source time. On a closed source, an end before the start crosses the loop seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	float CopySplineEndTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	int32 FreeStartPointCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	int32 FreeEndingPointCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	EPCGChildSplineEndpointTangentMode StartTangentMode = EPCGChildSplineEndpointTangentMode::Automatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	EPCGChildSplineEndpointTangentMode EndTangentMode = EPCGChildSplineEndpointTangentMode::Automatic;

	/** World-space Z offset applied to every point copied from the parent spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child Spline")
	float ZOffset = 0.0f;

	/** Rebuild from the attached parent spline. Safe to use after editing the parent. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Child Spline")
	void RefreshSpline();

	/** Get the external tagged spline, or the attached parent spline when external copying is disabled. */
	UFUNCTION(BlueprintPure, Category = "Child Spline|Source")
	USplineComponent* GetSourceSpline() const;

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual ESplineLoopMode GetSplineLoopMode() override { return ESplineLoopMode::OpenLoopOnly; }

private:
	uint32 CalculateSourceHash() const;

	UPROPERTY(Transient)
	int32 GeneratedCopiedPointCount = 0;

	UPROPERTY(Transient)
	int32 PreservedFreeStartPointCount = 0;

	UPROPERTY(Transient)
	int32 PreservedFreeEndPointCount = 0;

	uint32 LastSourceHash = 0;
	bool bRefreshingSpline = false;
};
