// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGDynMeshPainterFromPoints.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterFalloff : uint8
{
	Hard UMETA(DisplayName="Hard"),
	Linear UMETA(DisplayName="Linear"),
	Smooth UMETA(DisplayName="Smooth")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterPointReduction : uint8
{
	Max UMETA(DisplayName="Max"),
	Min UMETA(DisplayName="Min"),
	Add UMETA(DisplayName="Add"),
	Multiply UMETA(DisplayName="Multiply")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterRadiusSource : uint8
{
	Bounds UMETA(DisplayName="Point Bounds"),
	Attribute UMETA(DisplayName="Attribute")
};

USTRUCT()
struct FPCGUtilsDynMeshPreparedPaintPoint
{
	GENERATED_BODY()

	/** Point transform used by bounds-fitted oriented ellipsoids. */
	UPROPERTY()
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY()
	FVector LocalBoundsCenter = FVector::ZeroVector;

	UPROPERTY()
	FVector LocalOuterRadii = FVector::OneVector;

	/** Uniform world-space radius used by Attribute mode. */
	UPROPERTY()
	float OuterRadius = 1.0f;

	/** Inner core as a fraction of the outer shape. */
	UPROPERTY()
	float InnerRadiusFraction = 0.0f;

	UPROPERTY()
	float FalloffPower = 1.0f;

	UPROPERTY()
	float Value = 0.0f;

	UPROPERTY()
	bool bUseBoundsShape = true;
};

/** A scalar field made from prepared per-point spherical or ellipsoidal brushes in PCG world space. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPainterFromPointsFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FPCGUtilsDynMeshPreparedPaintPoint> Points;

	UPROPERTY()
	FPCGAttributePropertyInputSelector ValueSelector;

	UPROPERTY()
	EPCGUtilsDynMeshPainterRadiusSource RadiusSource =
		EPCGUtilsDynMeshPainterRadiusSource::Bounds;

	UPROPERTY()
	FPCGAttributePropertyInputSelector RadiusSelector;

	UPROPERTY()
	bool bUseInnerRadius = false;

	UPROPERTY()
	FPCGAttributePropertyInputSelector InnerRadiusSelector;

	UPROPERTY()
	bool bUseFalloffPowerAttribute = true;

	UPROPERTY()
	FPCGAttributePropertyInputSelector FalloffPowerSelector;

	UPROPERTY()
	float ConstantFalloffPower = 1.0f;

	UPROPERTY()
	EPCGUtilsDynMeshPainterFalloff Falloff = EPCGUtilsDynMeshPainterFalloff::Smooth;

	UPROPERTY()
	EPCGUtilsDynMeshPainterPointReduction Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;

	UPROPERTY()
	bool bClampValue = true;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Converts PCG points into reusable per-point spherical or ellipsoidal scalar brushes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPainterFromPointsProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPainterFromPointsProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPainterFromPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Scalar value read once per input point while the Painter is prepared. Defaults to $Density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector ValueSelector;

	/** Point Bounds creates an oriented ellipsoid from each point's transform and local bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radius", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterRadiusSource RadiusSource =
		EPCGUtilsDynMeshPainterRadiusSource::Bounds;

	/** Per-point uniform world-space outer radius. Defaults to the Radius attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radius",
		meta=(PCG_Overridable, EditCondition="RadiusSource==EPCGUtilsDynMeshPainterRadiusSource::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector RadiusSelector;

	/** Enable a solid inner core before the falloff begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radius", meta=(PCG_Overridable))
	bool bUseInnerRadius = false;

	/** Per-point world-space inner radius. Defaults to the InnerRadius attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radius",
		meta=(PCG_Overridable, EditCondition="bUseInnerRadius", EditConditionHides))
	FPCGAttributePropertyInputSelector InnerRadiusSelector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterFalloff Falloff = EPCGUtilsDynMeshPainterFalloff::Smooth;

	/** Read a per-point exponent for the falloff curve. Defaults to $Steepness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="Falloff!=EPCGUtilsDynMeshPainterFalloff::Hard"))
	bool bUseFalloffPowerAttribute = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="Falloff!=EPCGUtilsDynMeshPainterFalloff::Hard && bUseFalloffPowerAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector FalloffPowerSelector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, ClampMin="0.0001", EditCondition="Falloff!=EPCGUtilsDynMeshPainterFalloff::Hard && !bUseFalloffPowerAttribute", EditConditionHides))
	float ConstantFalloffPower = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterPointReduction Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable, DisplayName="Clamp Value"))
	bool bClampValue = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
