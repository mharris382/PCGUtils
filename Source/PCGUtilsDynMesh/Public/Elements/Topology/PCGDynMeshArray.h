// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGSplineStruct.h"
#include "Elements/Deform/PCGTransformDynMesh.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDynMeshArray.generated.h"

namespace PCGDynMeshArrayConstants
{
	inline const FName PointsInputPin = TEXT("Points");
	inline const FName SplineInputPin = TEXT("Spline");
	inline const FName BoundsInputPin = TEXT("Bounds");
}

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshArrayMode : uint8
{
	/** Blender-style fixed count using Relative Offset and Constant Offset. Count includes the source copy. */
	Count,
	/** Adds one copy at every supplied point transform, like an object-offset placement list. */
	Points,
	/** Adds regularly spaced, tangent-oriented copies along one PCG spline. */
	Spline,
	/** Adds as many translated copies as fit inside one oriented point bound. */
	FitBounds
};

/** A value snapshot of one oriented PCG point bound; safe to retain in a deferred Builder operation. */
struct FPCGUtilsDynMeshArrayBounds
{
	bool bIsSet = false;
	FTransform WorldTransform = FTransform::Identity;
	FVector LocalMin = FVector::ZeroVector;
	FVector LocalMax = FVector::ZeroVector;
};

/**
 * Blender-style Array modifier for DynMesh and deferred Builder pipelines.
 *
 * Every added copy preserves mesh overlays, material IDs, PolyGroups, weight layers, and the source material
 * list. Selection input arrays only the selected triangle region and welds the combined result back afterward.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh selection selector array duplicate repeat copies builder modifier points spline curve bounds fit"))
class PCGUTILSDYNMESH_API UPCGDynMeshArraySettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshArray"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array", meta=(PCG_Overridable))
	EPCGUtilsDynMeshArrayMode Mode = EPCGUtilsDynMeshArrayMode::Count;

	/** Total number of copies in Count mode, including the unchanged source copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array",
		meta=(PCG_Overridable, ClampMin="1", EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Count", EditConditionHides))
	int32 Count = 2;

	/** Translation per copy in Unreal units, expressed in Space. Combined with Relative Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Count || Mode==EPCGUtilsDynMeshArrayMode::FitBounds", EditConditionHides))
	FVector ConstantOffset = FVector::ZeroVector;

	/** Translation per copy as a component-wise multiple of the source bounds size, matching Blender Relative Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Count || Mode==EPCGUtilsDynMeshArrayMode::FitBounds", EditConditionHides))
	FVector RelativeOffset = FVector(1.0, 0.0, 0.0);

	/** Coordinate frame for Count/Bounds offsets and for the source pivot used by Point/Spline placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(PCG_Overridable))
	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::BuilderLocal;

	/** Retains the source at its current transform in addition to Point or Spline placements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Points || Mode==EPCGUtilsDynMeshArrayMode::Spline", EditConditionHides))
	bool bKeepOriginal = false;

	/** Distance between spline copies. Must be greater than zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline",
		meta=(PCG_Overridable, ClampMin="0.001", Units="cm", EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Spline", EditConditionHides))
	double SplineSpacing = 100.0;

	/** Redistributes the spline copies so the first and last land exactly at the spline endpoints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Spline", EditConditionHides))
	bool bFitSplineSpacing = false;

	/** Applies spline scale to placed copies. Rotation/roll always orient each copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::Spline", EditConditionHides))
	bool bUseSplineScale = false;

	/** In Bounds mode, enlarges the computed step so the last copy touches the opposite usable bound exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshArrayMode::FitBounds", EditConditionHides))
	bool bFitBoundsPerfectly = false;

	/** Safety ceiling for generated copies in every mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Advanced", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="1"))
	int32 MaxCopies = 10000;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshArrayElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshArrayOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	bool bIsValid = true;
	EPCGUtilsDynMeshArrayMode Mode = EPCGUtilsDynMeshArrayMode::Count;
	int32 Count = 2;
	FVector ConstantOffset = FVector::ZeroVector;
	FVector RelativeOffset = FVector(1.0, 0.0, 0.0);
	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::BuilderLocal;
	bool bKeepOriginal = false;
	double SplineSpacing = 100.0;
	bool bFitSplineSpacing = false;
	bool bUseSplineScale = false;
	bool bFitBoundsPerfectly = false;
	int32 MaxCopies = 10000;
	TArray<FTransform> PointWorldTransforms;
	FPCGSplineStruct Spline;
	FPCGUtilsDynMeshArrayBounds Bounds;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
