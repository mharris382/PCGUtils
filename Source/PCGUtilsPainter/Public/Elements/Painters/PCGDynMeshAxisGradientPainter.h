// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Curves/CurveFloat.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "PCGDynMeshAxisGradientPainter.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterCoordinateSpace : uint8
{
	Local UMETA(DisplayName="DynMesh Local"),
	World UMETA(DisplayName="World")
};

/** Where the Axis Gradient origin comes from. */
UENUM(BlueprintType)
enum class EPCGUtilsAxisGradientOriginMode : uint8
{
	Absolute UMETA(DisplayName="Absolute", ToolTip="Origin is a position in the gradient's coordinate space."),
	BoundsRelative UMETA(DisplayName="Bounds Relative", ToolTip="Origin Alignment picks a point of the mesh's bounding box on each axis; Origin is added to it as an offset.")
};

namespace PCGUtilsAxisGradientBounds
{
	/** How Measure found the span. */
	enum class ESpanCase : uint8
	{
		/** The line through the origin crosses the box; the span is where the ray leaves it. */
		Direct,
		/** The line through the origin misses the box; the nearest parallel line that touches it was used. */
		Shifted,
		/** The box lies wholly behind the origin, so the ray never leaves it; the box's thickness along the axis was used. */
		Behind
	};

	/**
	 * The distance from Origin, along unit Axis, at which the ray leaves Box through its far side.
	 * If the line through Origin misses Box, the nearest parallel line that touches Box is used instead (distance
	 * still measured along Axis, from Origin's position on that line). If Box is entirely behind the origin, returns the
	 * box's thickness along the line. Never negative. OutLineOrigin receives the origin moved onto the line used.
	 */
	PCGUTILSPAINTER_API double Measure(const FBox& Box, const FVector& Origin, const FVector& Axis, ESpanCase* OutCase = nullptr,
		FVector* OutLineOrigin = nullptr);
}

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshAxisGradientPainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector Origin = FVector::ZeroVector;

	UPROPERTY()
	EPCGUtilsAxisGradientOriginMode OriginMode = EPCGUtilsAxisGradientOriginMode::Absolute;

	UPROPERTY()
	FPCGUtilsJustificationDetails OriginAlignment;

	UPROPERTY()
	FVector Axis = FVector::UpVector;

	UPROPERTY()
	bool bDistancesRelativeToBounds = false;

	UPROPERTY()
	float StartDistance = 0.0f;

	UPROPERTY()
	float EndDistance = 100.0f;
	
	UPROPERTY()
	float GradientStart = 0.0f;

	UPROPERTY()
	float GradientEnd = 1.0f;
	
	UPROPERTY()
	TObjectPtr<UCurveFloat> Curve;
	
	UPROPERTY()
	bool bInvert = false;

	UPROPERTY()
	EPCGUtilsDynMeshPainterCoordinateSpace CoordinateSpace =
		EPCGUtilsDynMeshPainterCoordinateSpace::Local;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Creates a clamped linear scalar gradient projected along an axis. The origin can be absolute or placed on the
 * mesh's bounding box, and Start/End can be fractions of how far the axis runs through the box from the origin.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters", meta=(Keywords="DynMesh Painter axis gradient ramp linear height"))
class PCGUTILSPAINTER_API UPCGDynMeshAxisGradientPainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshAxisGradientPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPCGDynMeshAxisGradientPainterProviderSettings();

	/** Absolute: Origin is a position. Bounds Relative: Origin Alignment places it on the mesh's bounding box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsAxisGradientOriginMode OriginMode = EPCGUtilsAxisGradientOriginMode::Absolute;

	/**
	 * Per axis, the point of the mesh's bounding box (in Coordinate Space) the origin sits on. Use To: Min, Center,
	 * Max, or Custom (0 = min, 1 = max); an axis that is off stays at 0. The box is the whole mesh's, not the selection's.
	 * From only matters through To = Same.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="OriginMode == EPCGUtilsAxisGradientOriginMode::BoundsRelative", EditConditionHides))
	FPCGUtilsJustificationDetails OriginAlignment;

	/** Absolute: the gradient origin. Bounds Relative: an offset added to the aligned point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FVector Origin = FVector::ZeroVector;

	/** Direction the gradient runs. Normalized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FVector Axis = FVector::UpVector;

	/**
	 * Start and End become multiples of the span: the distance a ray from the origin along Axis travels before it
	 * leaves the mesh's bounding box through the far side. Origin at the bottom with Axis up: 0 = bottom, 1 = top.
	 * An origin whose line misses the box measures along the nearest parallel line that touches it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	bool bDistancesRelativeToBounds = false;

	/** Distance along Axis where the gradient is 0 (a multiple of the span when Distances Relative To Bounds is on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float StartDistance = 0.0f;

	/** Distance along Axis where the gradient is 1 (a multiple of the span when Distances Relative To Bounds is on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float EndDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float GradientStart = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float GradientEnd = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	TSoftObjectPtr<UCurveFloat> Curve;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterCoordinateSpace CoordinateSpace =
		EPCGUtilsDynMeshPainterCoordinateSpace::Local;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
