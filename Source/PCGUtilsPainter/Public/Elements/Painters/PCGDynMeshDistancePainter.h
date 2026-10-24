// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGDynMeshDistancePainter.generated.h"

/** Which part of each input point distances are measured to. Same choices as PCGEx's Target distance method. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterDistanceShape : uint8
{
	Center UMETA(DisplayName="Center", ToolTip="The point's location."),
	SphereBounds UMETA(DisplayName="Sphere Bounds", ToolTip="A sphere at the point's location whose radius is the length of its scaled extents."),
	BoxBounds UMETA(DisplayName="Box Bounds", ToolTip="The point's oriented bounds box. A vertex inside the box is at distance 0.")
};

/** How a distance is measured. Same choices as PCGEx's distance Type. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterDistanceMetric : uint8
{
	Euclidean UMETA(DisplayName="Euclidean", ToolTip="Straight-line distance: round dots."),
	Manhattan UMETA(DisplayName="Manhattan", ToolTip="Sum of the X, Y and Z differences: diamond (octahedron) shaped dots."),
	Chebyshev UMETA(DisplayName="Chebyshev", ToolTip="Largest of the X, Y and Z differences: square (cube) shaped dots.")
};

/** How the values of several points in range of one vertex become one value. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterDistanceBlend : uint8
{
	Nearest UMETA(DisplayName="Nearest", ToolTip="Only the nearest point in range contributes."),
	Max UMETA(DisplayName="Max", ToolTip="Largest value from all points in range."),
	Min UMETA(DisplayName="Min", ToolTip="Smallest value from all points in range."),
	Average UMETA(DisplayName="Average", ToolTip="Mean value of all points in range."),
	Add UMETA(DisplayName="Add", ToolTip="Sum of the values of all points in range."),
	Multiply UMETA(DisplayName="Multiply", ToolTip="Product of the values of all points in range.")
};

/** Shape of the blend from Value At Min Radius to Value At Max Radius. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterDistanceFalloff : uint8
{
	Linear UMETA(DisplayName="Linear"),
	Smooth UMETA(DisplayName="Smooth", ToolTip="Smoothstep: eases in at both radii."),
	Power UMETA(DisplayName="Power", ToolTip="Blend = t ^ Exponent, where t is 0 at Min Radius and 1 at Max Radius."),
	Exponential UMETA(DisplayName="Exponential", ToolTip="Exponential decay normalized to reach the end value exactly at Max Radius. Positive Steepness changes fast near Min Radius, negative changes fast near Max Radius."),
	Curve UMETA(DisplayName="Curve", ToolTip="Blend read from Falloff Curve at t, where t is 0 at Min Radius and 1 at Max Radius.")
};

/** What a vertex closer to a point than Min Radius gets from that point. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterDistanceInsideMin : uint8
{
	SolidCore UMETA(DisplayName="Solid Core", ToolTip="Value At Min Radius, so the dot is filled."),
	OutOfRange UMETA(DisplayName="Out Of Range", ToolTip="The point is not in range, so the dot is a ring.")
};

/** One input point as the Distance Painter stores it: world transform and local bounds. */
USTRUCT()
struct FPCGUtilsPainterDistancePoint
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	FVector BoundsMin = -FVector::OneVector;

	UPROPERTY()
	FVector BoundsMax = FVector::OneVector;
};

/** Configuration of a Distance Painter: world-space points and how distance to them becomes a value. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshDistancePainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FPCGUtilsPainterDistancePoint> Points;

	UPROPERTY()
	EPCGUtilsPainterDistanceShape Target = EPCGUtilsPainterDistanceShape::Center;

	UPROPERTY()
	EPCGUtilsPainterDistanceMetric Type = EPCGUtilsPainterDistanceMetric::Euclidean;

	UPROPERTY()
	bool bOverlapIsZero = true;

	UPROPERTY()
	double MinRadius = 0.0;

	UPROPERTY()
	double MaxRadius = 200.0;

	UPROPERTY()
	EPCGUtilsPainterDistanceInsideMin InsideMinRadius = EPCGUtilsPainterDistanceInsideMin::SolidCore;

	UPROPERTY()
	EPCGUtilsPainterDistanceFalloff Falloff = EPCGUtilsPainterDistanceFalloff::Linear;

	UPROPERTY()
	float Exponent = 2.0f;

	UPROPERTY()
	float Steepness = 4.0f;

	/** Resolved falloff curve (inline or external), only used by the Curve falloff. */
	UPROPERTY()
	FRichCurve Curve;

	UPROPERTY()
	float ValueAtMinRadius = 1.0f;

	UPROPERTY()
	float ValueAtMaxRadius = 0.0f;

	UPROPERTY()
	float OutOfRangeValue = 0.0f;

	UPROPERTY()
	EPCGUtilsPainterDistanceBlend Blend = EPCGUtilsPainterDistanceBlend::Nearest;

	UPROPERTY()
	bool bClampValue = true;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Paints by distance from every vertex to the PCG points in range. Distance is measured to each point's center,
 * bounds sphere or oriented bounds box, as Euclidean, Manhattan or Chebyshev distance.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters", meta=(Keywords="Painter distance nearest point points radius range falloff ring dot mask DynMesh"))
class PCGUTILSPAINTER_API UPCGDynMeshDistancePainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshDistancePainterProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshDistancePainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Which part of each point distance is measured to. A vertex has no bounds, so there is no Source setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distance", meta=(PCG_Overridable))
	EPCGUtilsPainterDistanceShape Target = EPCGUtilsPainterDistanceShape::Center;

	/** How distance is measured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distance", meta=(PCG_Overridable))
	EPCGUtilsPainterDistanceMetric Type = EPCGUtilsPainterDistanceMetric::Euclidean;

	/** A vertex inside a point's bounds sphere is at distance 0. Off, it is measured to the sphere's surface from inside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distance",
		meta=(PCG_Overridable, EditCondition="Target==EPCGUtilsPainterDistanceShape::SphereBounds", EditConditionHides))
	bool bOverlapIsZero = true;

	/** Distance at which the falloff starts (Value At Min Radius). Must be at most Max Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(PCG_Overridable, ClampMin="0"))
	double MinRadius = 0.0;

	/** Distance at which the falloff ends (Value At Max Radius). Points farther away are out of range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(PCG_Overridable, ClampMin="0"))
	double MaxRadius = 200.0;

	/** What a vertex closer to a point than Min Radius gets from that point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(PCG_Overridable))
	EPCGUtilsPainterDistanceInsideMin InsideMinRadius = EPCGUtilsPainterDistanceInsideMin::SolidCore;

	/** Shape of the blend from Value At Min Radius to Value At Max Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(PCG_Overridable))
	EPCGUtilsPainterDistanceFalloff Falloff = EPCGUtilsPainterDistanceFalloff::Linear;

	/** Blend = t ^ Exponent. Above 1 holds the start value longer, below 1 leaves it sooner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range",
		meta=(PCG_Overridable, ClampMin="0.0001", EditCondition="Falloff==EPCGUtilsPainterDistanceFalloff::Power", EditConditionHides))
	float Exponent = 2.0f;

	/** Exponential rate. Positive changes fast near Min Radius, negative changes fast near Max Radius, 0 is linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range",
		meta=(PCG_Overridable, EditCondition="Falloff==EPCGUtilsPainterDistanceFalloff::Exponential", EditConditionHides))
	float Steepness = 4.0f;

	/** X is 0 at Min Radius and 1 at Max Radius. Y is the blend: 0 is Value At Min Radius, 1 is Value At Max Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range",
		meta=(EditCondition="Falloff==EPCGUtilsPainterDistanceFalloff::Curve", EditConditionHides))
	FRuntimeFloatCurve FalloffCurve;

	/** Value a point gives at Min Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Value", meta=(PCG_Overridable))
	float ValueAtMinRadius = 1.0f;

	/** Value a point gives at Max Radius. 1 to 0 is a soft dot, 0 to 1 a ring, 1 to 1 a hard dot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Value", meta=(PCG_Overridable))
	float ValueAtMaxRadius = 0.0f;

	/** Value of a vertex with no point in range. Separate from the in-range values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Value", meta=(PCG_Overridable))
	float OutOfRangeValue = 0.0f;

	/** How the values of several points in range of one vertex combine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Value", meta=(PCG_Overridable, DisplayName="Overlapping Points"))
	EPCGUtilsPainterDistanceBlend Blend = EPCGUtilsPainterDistanceBlend::Nearest;

	/** Clamp the final value to [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Value", meta=(PCG_Overridable, DisplayName="Clamp Value"))
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
