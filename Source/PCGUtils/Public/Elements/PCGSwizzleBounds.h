#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGSwizzleBounds.generated.h"

/** Which pair of local axes Swizzle Bounds exchanges. The third axis is the one the frame turns about. */
UENUM(BlueprintType)
enum class EPCGUtilsSwizzleAxes : uint8
{
	XY UMETA(DisplayName="X <> Y", ToolTip="Exchange local X and Y. The frame turns 90 degrees about local Z."),
	XZ UMETA(DisplayName="X <> Z", ToolTip="Exchange local X and Z. The frame turns 90 degrees about local Y."),
	YZ UMETA(DisplayName="Y <> Z", ToolTip="Exchange local Y and Z. The frame turns 90 degrees about local X.")
};

/** What Swizzle Bounds keeps fixed besides the world-space box (which never moves). */
UENUM(BlueprintType)
enum class EPCGUtilsSwizzlePivot : uint8
{
	PointLocation UMETA(DisplayName="Point Location", ToolTip="The point's world location stays put. An off-center bounds offset turns with the axes, e.g. a local center of (5,0,0) becomes (0,-5,0) for X <> Y."),
	LocalCenter UMETA(DisplayName="Local Center", ToolTip="The bounds' local center keeps its values, e.g. (5,0,0) stays (5,0,0). The point's location moves so the box stays where it was. Identical to Point Location for centered bounds.")
};

namespace PCGUtilsSwizzleBounds
{
	/**
	 * Re-expresses one point's box in a local frame whose two chosen axes are exchanged.
	 * The rotation, scale and bounds all change; the box the point occupies in world space does not.
	 * The new frame is always a proper rotation (no mirroring): the first axis of the pair takes the
	 * second's direction and the second axis takes the first's reversed. bFlip reverses both, which
	 * turns the frame the other way (a 180 degree difference about the third axis).
	 * Pivot decides whether the point's location or the bounds' local center is kept.
	 */
	PCGUTILS_API void Apply(EPCGUtilsSwizzleAxes Axes, bool bFlip, EPCGUtilsSwizzlePivot Pivot,
		FTransform& InOutTransform, FVector& InOutBoundsMin, FVector& InOutBoundsMax);
}

/**
 * Exchanges two local axes of every point without changing the box the point occupies.
 * Use it before a fit-to-bounds step (e.g. PCGEx mesh fitting) when a mesh is fitted into the right
 * box but along the wrong axis: after swizzling, the box's long side lies along the other axis label.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils", meta=(Keywords="Point Points Swizzle Bounds Swap Axis Axes Rotate Orient Fit PCGEx"))
class PCGUTILS_API UPCGSwizzleBoundsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SwizzleBounds")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Points); }
#endif

	/** The pair of local axes to exchange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGUtilsSwizzleAxes Axes = EPCGUtilsSwizzleAxes::XY;

	/**
	 * Turn the frame the other way. The box is unchanged either way; this decides which end of the
	 * exchanged axes points forward, so a fitted mesh comes out rotated 180 degrees about the third axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bFlip = false;

	/**
	 * The world-space box never moves. This picks what else stays fixed: the point's world location
	 * (default), or the bounds' local center values (the point's location moves to compensate).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGUtilsSwizzlePivot Preserve = EPCGUtilsSwizzlePivot::PointLocation;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILS_API FPCGSwizzleBoundsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
