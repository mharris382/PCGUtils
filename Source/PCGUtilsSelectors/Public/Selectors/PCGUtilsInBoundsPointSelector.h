// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Selectors/PCGUtilsPointSelector.h"

#include "PCGUtilsInBoundsPointSelector.generated.h"

class UPCGBasePointData;

/** How a point that has bounds is tested against a bound. A point with no bounds is only its position, so all three are then the same test. */
UENUM(BlueprintType)
enum class EPCGUtilsSelectorContainment : uint8
{
	/** The point's position is inside a bound. */
	Center,
	/** Any part of the point's box touches a bound. */
	AnyPart UMETA(DisplayName="Any Part"),
	/** The whole of the point's box is inside one bound. */
	FullyInside UMETA(DisplayName="Fully Inside"),
};

/** One oriented box in the world, as the In Bounds maths reads it. */
struct PCGUTILSSELECTORS_API FPCGUtilsSelectorWorldBox
{
	FVector Center = FVector::ZeroVector;
	FVector Axis[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
	FVector Half = FVector::ZeroVector;

	FPCGUtilsSelectorWorldBox() = default;
	/** LocalBounds placed by Transform. A negative scale flips nothing that matters here: half sizes are absolute. */
	FPCGUtilsSelectorWorldBox(const FTransform& Transform, const FBox& LocalBounds);

	/** How far the box reaches from its centre along Direction (unit length). */
	double Radius(const FVector& Direction) const;
};

namespace PCGUtilsSelectorBoxMaths
{
	/** Whether WorldPosition is inside or on LocalBounds placed by Transform. */
	PCGUTILSSELECTORS_API bool BoxContains(const FTransform& Transform, const FBox& LocalBounds, const FVector& WorldPosition);

	/** Whether two oriented boxes touch or overlap (separating axis test over 15 axes). */
	PCGUTILSSELECTORS_API bool BoxesOverlap(const FPCGUtilsSelectorWorldBox& A, const FPCGUtilsSelectorWorldBox& B);
}

/**
 * The In Bounds point selector: selects the points lying in the bounds of any point of its point data. Reads
 * transforms and bounds only, so it never asks an element point set for real point data.
 */
UCLASS(ClassGroup=(Procedural))
class PCGUTILSSELECTORS_API UPCGUtilsInBoundsPointSelectorData : public UPCGUtilsPointSelectorData
{
	GENERATED_BODY()

public:
	/** The points whose bounds select: each point's Bounds Min / Max, placed by its transform. */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> PointData;

	UPROPERTY()
	EPCGUtilsSelectorContainment Containment = EPCGUtilsSelectorContainment::Center;

	/** Grows (or, negative, shrinks) every bound on all sides, in world units. */
	UPROPERTY()
	double Expand = 0.0;

	/** Reads the bounds once. Warns (once) about bounds that are inside out after Expand; they are skipped. */
	virtual bool Prepare(FPCGContext* InContext) override;

	virtual TSharedPtr<const FPCGUtilsPointSelectorOperation> CreateOperation(FPCGContext* Context) const override;

	/** One bound, ready to test against. */
	struct FBound
	{
		FTransform Transform;
		FBox LocalBounds;
		FPCGUtilsSelectorWorldBox WorldBox;
	};
	struct FPrepared
	{
		TArray<FBound> Bounds;
		/** World box round every bound, for a cheap early out. */
		FBox Reach = FBox(ForceInit);
		int32 NumSkipped = 0;
	};

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;

private:
	TSharedPtr<const FPrepared> BuildPrepared() const;

	/** Set by Prepare, never changed after. */
	TSharedPtr<const FPrepared> Prepared;
};
