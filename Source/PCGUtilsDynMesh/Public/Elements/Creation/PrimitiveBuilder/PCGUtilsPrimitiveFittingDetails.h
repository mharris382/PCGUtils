// Copyright Max Harris
// Fitting/alignment structures adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsPrimitiveFittingDetails.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsFitMode : uint8
{
	/** No scaling; the primitive keeps its native size (after LocalTransform). */
	None,
	/** The same scale factor is applied to all three axes. */
	Uniform,
	/** Each axis picks its own scale-to-fit strategy. */
	Individual
};

UENUM(BlueprintType)
enum class EPCGUtilsScaleToFit : uint8
{
	/** Do not scale this axis. */
	None,
	/** Stretch to exactly fill the target bounds on this axis. */
	Fill,
	/** Use the smallest of the three per-axis fill ratios (keeps the primitive from overflowing any axis). */
	Min,
	/** Use the largest of the three per-axis fill ratios (guarantees the primitive fills at least one axis). */
	Max,
	/** Use the average of the three per-axis fill ratios. */
	Avg
};

UENUM(BlueprintType)
enum class EPCGUtilsJustifyFrom : uint8
{
	Min,
	Center,
	Max,
	/** The primitive's own local origin, unmoved. */
	Pivot,
	/** A fixed 0-1 fraction along the primitive's own bounds (0 = min, 0.5 = center, 1 = max). */
	Custom
};

UENUM(BlueprintType)
enum class EPCGUtilsJustifyTo : uint8
{
	/** Mirrors whichever anchor 'From' selected, onto the target bounds. */
	Same,
	Min,
	Center,
	Max,
	/** The seed's own local origin. */
	Pivot,
	/** A fixed 0-1 fraction along the target bounds (0 = min, 0.5 = center, 1 = max). */
	Custom
};

namespace PCGUtilsFitting
{
	/**
	 * Per-axis scale-to-fit factor resolution, shared by the uniform and per-axis-individual modes so the
	 * math has a single source of truth. MinMaxFit packs the three uniform options: X = Min, Y = Max, Z = Avg.
	 */
	void ScaleToFitAxis(
		EPCGUtilsScaleToFit Fit, int32 Axis, const FVector& TargetScale, const FVector& TargetSize,
		const FVector& CandidateSize, const FVector& MinMaxFit, FVector& OutScale);

	/** Full three-axis scale-to-fit pass. No-op when Mode == None: OutScale is left untouched. */
	void ScaleToFitAxes(
		EPCGUtilsFitMode Mode, EPCGUtilsScaleToFit UniformFit,
		EPCGUtilsScaleToFit FitX, EPCGUtilsScaleToFit FitY, EPCGUtilsScaleToFit FitZ,
		const FVector& TargetSize, const FVector& TargetScale, const FBox& InBounds, FVector& OutScale);

	/**
	 * Per-axis justification translation. FromValue/ToValue are normalized positions (0 = bounds min,
	 * 0.5 = center, 1 = bounds max), consumed only by the Custom modes (and by To == Same when From == Custom).
	 */
	void JustifyAxis(
		EPCGUtilsJustifyFrom From, EPCGUtilsJustifyTo To, double FromValue, double ToValue, int32 Axis,
		const FVector& InCenter, const FVector& InSize, const FVector& OutCenter, const FVector& OutSize,
		FVector& OutTranslation);
}

USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsScaleToFitDetails
{
	GENERATED_BODY()

	/** How scaling is applied to fit within the seed's (padded) bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable))
	EPCGUtilsFitMode ScaleToFitMode = EPCGUtilsFitMode::Uniform;

	/** Uniform scaling strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, EditCondition = "ScaleToFitMode == EPCGUtilsFitMode::Uniform", EditConditionHides))
	EPCGUtilsScaleToFit ScaleToFit = EPCGUtilsScaleToFit::Min;

	/** Scaling strategy for the X axis when using Individual mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, EditCondition = "ScaleToFitMode == EPCGUtilsFitMode::Individual", EditConditionHides))
	EPCGUtilsScaleToFit ScaleToFitX = EPCGUtilsScaleToFit::None;

	/** Scaling strategy for the Y axis when using Individual mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, EditCondition = "ScaleToFitMode == EPCGUtilsFitMode::Individual", EditConditionHides))
	EPCGUtilsScaleToFit ScaleToFitY = EPCGUtilsScaleToFit::None;

	/** Scaling strategy for the Z axis when using Individual mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, EditCondition = "ScaleToFitMode == EPCGUtilsFitMode::Individual", EditConditionHides))
	EPCGUtilsScaleToFit ScaleToFitZ = EPCGUtilsScaleToFit::None;

	/** Resolves the fit scale factor for InBounds against a target of TargetSize*TargetScale. */
	void Process(const FVector& TargetSize, const FVector& TargetScale, const FBox& InBounds, FVector& OutScale) const;
};

USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsSingleJustifyDetails
{
	GENERATED_BODY()

	/** Reference point on the primitive being positioned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable))
	EPCGUtilsJustifyFrom From = EPCGUtilsJustifyFrom::Center;

	/** Fixed 'From' position (0 = bounds min, 0.5 = center, 1 = bounds max). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, DisplayName = " |- From Value", EditCondition = "From == EPCGUtilsJustifyFrom::Custom", EditConditionHides))
	double FromValue = 0.5;

	/** Target point in the seed's bounds to align to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable))
	EPCGUtilsJustifyTo To = EPCGUtilsJustifyTo::Same;

	/** Fixed 'To' position (0 = bounds min, 0.5 = center, 1 = bounds max). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
		meta = (PCG_Overridable, DisplayName = " |- To Value", EditCondition = "To == EPCGUtilsJustifyTo::Custom", EditConditionHides))
	double ToValue = 0.5;

	void JustifyAxis(int32 Axis, const FVector& InCenter, const FVector& InSize, const FVector& OutCenter, const FVector& OutSize, FVector& OutTranslation) const;
};

USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsJustificationDetails
{
	GENERATED_BODY()

	/** Enable justification on the X axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyX = true;

	/** X axis justification settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, EditCondition = "bDoJustifyX"))
	FPCGUtilsSingleJustifyDetails JustifyX;

	/** Enable justification on the Y axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyY = true;

	/** Y axis justification settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, EditCondition = "bDoJustifyY"))
	FPCGUtilsSingleJustifyDetails JustifyY;

	/** Enable justification on the Z axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyZ = true;

	/** Z axis justification settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, EditCondition = "bDoJustifyZ"))
	FPCGUtilsSingleJustifyDetails JustifyZ;

	/** Resolves the translation that moves FittedBounds' justification anchor onto TargetBounds' anchor. */
	void Process(const FBox& TargetBounds, const FBox& FittedBounds, FVector& OutTranslation) const;
};

/**
 * Combines scale-to-fit, justification, padding, and a local pre-transform into the single placement
 * transform used to append a primitive at a seed. Ported from PCGExtendedToolkit's
 * FPCGExFittingDetailsHandler / FPCGExLeanScaleToFitDetails / FPCGExLeanJustificationDetails, adapted to take
 * the seed's transform/bounds directly as parameters instead of through a PCGExData::FFacade lookup.
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsFittingDetails
{
	GENERATED_BODY()

	/** How to scale the primitive to fit within the seed's bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsScaleToFitDetails ScaleToFit;

	/** How to align the primitive within the seed's bounds after scaling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsJustificationDetails Justification;

	/**
	 * Insets (positive) or outsets (negative) the seed's local bounds on each axis before fitting runs.
	 * Not present in PCGExtendedToolkit's fitting model - added so two primitives built from the same seed
	 * bounds (e.g. a box and a padded box) can be combined with a boolean subtract to produce a wall/frame
	 * of consistent thickness.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable))
	FVector Padding = FVector::ZeroVector;

	/** Offset/rotation/scale applied to the primitive's own local geometry before fitting is computed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (PCG_Overridable))
	FTransform LocalTransform = FTransform::Identity;

	/**
	 * Computes the final placement transform for a primitive whose native (unfitted) local bounds are
	 * CandidateBounds, being placed at a seed with the given local-space transform and bounds.
	 */
	void ComputeLocalTransform(const FTransform& SeedTransform, const FBox& SeedLocalBounds, const FBox& CandidateBounds, FTransform& OutTransform) const;
};
