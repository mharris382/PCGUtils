// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Modifiers/PCGUtilsComponentModifier.h"
#include "PCGUtilsArrayModifier.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsArrayMode : uint8
{
	/** Copies step along a straight line: copy c sits at c x offset. Rotation and scale steps compound per copy about the copy's own pivot, but never turn the line. */
	Line,
	/** Blender's Object Offset: one step transform applied again for every copy, so rotation compounds into position. Circles, helices, fans, spirals. */
	Transform,
	/** Copies rotated about a centre point around an axis. */
	Circle,
};

UENUM(BlueprintType)
enum class EPCGUtilsArrayFit : uint8
{
	/** Exactly Count copies, the original included. */
	FixedCount,
	/** As many copies as fit in Length, measured from the start of the first copy to the start of the last (Blender's Fit Length). */
	FitLength,
};

UENUM(BlueprintType)
enum class EPCGUtilsModifierAxis : uint8
{
	X,
	Y,
	Z,
};

/**
 * Blender's Array modifier for PCG components: one marker becomes a row, ring or spiral of markers; one path
 * becomes several paths. All offsets are in the component's own frame.
 *
 * Step translation D = Relative Offset x the input's bounds size + Constant Offset, as in Blender.
 */
UCLASS(BlueprintType, meta=(DisplayName="Array"))
class PCGUTILS_API UPCGUtilsArrayModifier : public UPCGUtilsComponentModifier
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array")
	EPCGUtilsArrayMode Mode = EPCGUtilsArrayMode::Line;

	/** How the number of copies is decided. Circle always uses Count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array", meta=(EditCondition="Mode != EPCGUtilsArrayMode::Circle", EditConditionHides))
	EPCGUtilsArrayFit FitType = EPCGUtilsArrayFit::FixedCount;

	/** Number of copies, the original included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array", meta=(ClampMin="1", UIMin="1", UIMax="64",
		EditCondition="Mode == EPCGUtilsArrayMode::Circle || FitType == EPCGUtilsArrayFit::FixedCount", EditConditionHides))
	int32 Count = 2;

	/** Length to fill, in component space. Count = floor(Length / step length) + 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Array", meta=(ClampMin="0", Units="cm",
		EditCondition="Mode != EPCGUtilsArrayMode::Circle && FitType == EPCGUtilsArrayFit::FitLength", EditConditionHides))
	double Length = 500.0;

	/** Add a step proportional to the input's size (its bounds, per axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(InlineEditConditionToggle))
	bool bUseRelativeOffset = true;

	/** Step as a factor of the input's bounds size, per axis. (1, 0, 0) places copies end to end along X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(EditCondition="bUseRelativeOffset && Mode != EPCGUtilsArrayMode::Circle"))
	FVector RelativeOffset = FVector(1.0, 0.0, 0.0);

	/** Add a fixed step, whatever the input's size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(InlineEditConditionToggle))
	bool bUseConstantOffset = false;

	/** Fixed step in component space. In Line mode, the scene gizmo's move edits this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(EditCondition="bUseConstantOffset && Mode != EPCGUtilsArrayMode::Circle", Units="cm"))
	FVector ConstantOffset = FVector::ZeroVector;

	/** Translation of the step transform (Blender's offset object location). Transform mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(EditCondition="Mode == EPCGUtilsArrayMode::Transform", EditConditionHides, Units="cm"))
	FVector ObjectOffset = FVector::ZeroVector;

	/** Rotation added per copy. Line: about each copy's own pivot. Transform: part of the step transform, so it also turns the path the copies follow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(EditCondition="Mode != EPCGUtilsArrayMode::Circle", EditConditionHides))
	FRotator RotationStep = FRotator::ZeroRotator;

	/** Scale multiplied in per copy (copy c has ScaleStep^c). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset", meta=(EditCondition="Mode != EPCGUtilsArrayMode::Circle", EditConditionHides))
	FVector ScaleStep = FVector::OneVector;

	/** Point the copies turn about, in component space. The scene gizmo moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(EditCondition="Mode == EPCGUtilsArrayMode::Circle", EditConditionHides, Units="cm"))
	FVector CircleCenter = FVector(-200.0, 0.0, 0.0);

	/** Component-space axis the copies turn around. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(EditCondition="Mode == EPCGUtilsArrayMode::Circle", EditConditionHides))
	EPCGUtilsModifierAxis CircleAxis = EPCGUtilsModifierAxis::Z;

	/** Angle the copies span. At 360 (or more) the copies divide the ring evenly with no duplicate at the seam; less spreads them from the original to the last copy inclusive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(EditCondition="Mode == EPCGUtilsArrayMode::Circle", EditConditionHides, Units="deg", UIMin="-360", UIMax="360"))
	double TotalAngle = 360.0;

	/** Markers only: turn each copy to face along the ring. Off keeps the original orientation and only moves copies. Paths always turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(EditCondition="Mode == EPCGUtilsArrayMode::Circle", EditConditionHides))
	bool bAlignToRing = true;

	/** Markers: drop a copy's point when it lands within Merge Distance of the same point in the previous copy (Blender's Merge). Path merging is not built yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge")
	bool bMerge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge", meta=(EditCondition="bMerge", ClampMin="0", Units="cm"))
	double MergeDistance = 1.0;

	/** Also compare the last copy with the first, which removes the duplicate at a closed ring's seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge", meta=(EditCondition="bMerge"))
	bool bMergeFirstAndLast = false;

	/** Write each copy's number (original = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(InlineEditConditionToggle))
	bool bWriteCopyIndex = false;

	/**
	 * Optional. int32 attribute holding the copy number, original = 0. Markers: one value per point (elements domain).
	 * Paths: on each path's @Data, since every copy is its own path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(EditCondition="bWriteCopyIndex"))
	FName CopyIndexAttributeName = TEXT("ArrayIndex");

	//~ UPCGUtilsComponentModifier
	virtual EPCGUtilsModifierTarget GetSupportedTargets() const override { return EPCGUtilsModifierTarget::Marker | EPCGUtilsModifierTarget::Path; }
	virtual void ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const override;
	virtual void ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const override;

	/** The step translation D for an input of the given component-space bounds size. */
	FVector GetStepTranslation(const FVector& BoundsSize) const;

	/**
	 * Per-copy matrices in component space (row-vector convention: a component-space point times the matrix), copy 0
	 * first and always the identity. Line mode's rotation and scale are about the component-space origin here; the
	 * points path moves them to each point's pivot.
	 */
	TArray<FMatrix> ComputeCopyMatrices(const FVector& BoundsSize, const FPCGUtilsModifierContext* Context = nullptr) const;

	/** Line mode only: the pure rotation/scale part of copy c, and its translation, kept apart. */
	void GetLineCopyParts(int32 CopyIndex, const FVector& BoundsSize, FMatrix& OutRotationScale, FVector& OutTranslation) const;

	int32 ComputeCount(const FVector& BoundsSize, const FPCGUtilsModifierContext* Context) const;

	static FVector AxisVector(EPCGUtilsModifierAxis Axis);
};
