// Copyright Max Harris
// Dot comparison settings adapted from PCGExtendedToolkit (FPCGExDotComparisonDetails),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshSelectionComparison.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGUtilsDynMeshDotComparison.generated.h"

class FArchiveCrc32;
class UPCGData;

/** How a direction threshold is written. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshAngularDomain : uint8
{
	Scalar UMETA(DisplayName="Scalar", ToolTip="The threshold is a dot product: 1 same direction, 0 perpendicular, -1 opposite."),
	Degrees UMETA(DisplayName="Degrees", ToolTip="The threshold is an angle: 0 same direction, 90 perpendicular, 180 opposite.")
};

/** Where a per-element setting takes its value from. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshInputValueType : uint8
{
	Constant UMETA(DisplayName="Constant", ToolTip="Use the value set on the node."),
	Attribute UMETA(DisplayName="Attribute", ToolTip="Read the value from the input data.")
};

/**
 * Compares how closely two directions agree against a threshold. The threshold is a constant or an attribute,
 * written as a dot product or as an angle.
 *
 * A signed comparison maps both sides through (1 + dot) / 2 before comparing, so the operators read the same
 * way in either domain; an unsigned comparison compares magnitudes, treating opposite directions as the same.
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshDotComparison
{
	GENERATED_BODY()

	FPCGUtilsDynMeshDotComparison();

	/** Whether thresholds are dot products (-1 to 1) or angles in degrees (0 to 180). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable))
	EPCGUtilsDynMeshAngularDomain Domain = EPCGUtilsDynMeshAngularDomain::Scalar;

	/** How the measured agreement is compared with the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable))
	EPCGUtilsDynMeshDistanceComparison Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrGreater;

	/** Compare magnitudes, so a direction and its opposite count as the same. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable))
	bool bUnsignedComparison = false;

	/** Whether the threshold is the constant below or is read from an attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_NotOverridable, DisplayName="Threshold Input"))
	EPCGUtilsDynMeshInputValueType ThresholdInput = EPCGUtilsDynMeshInputValueType::Constant;

	/**
	 * Attribute the threshold is read from. Numeric (double), read as a dot product or as degrees according to
	 * Domain. Required when Threshold Input is Attribute, unused otherwise. Which data it is read from is stated
	 * by the node that owns this setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, DisplayName="Threshold (Attr)",
			EditCondition="ThresholdInput != EPCGUtilsDynMeshInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector ThresholdAttribute;

	/** Threshold as a dot product. 1 same direction, 0 perpendicular, -1 opposite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, DisplayName="Scalar", ClampMin=-1, ClampMax=1,
			EditCondition="ThresholdInput == EPCGUtilsDynMeshInputValueType::Constant && Domain == EPCGUtilsDynMeshAngularDomain::Scalar", EditConditionHides))
	double DotConstant = 0.9;

	/** How far from the threshold still counts as equal, for the Nearly Equal comparisons, as a dot product. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, DisplayName="Tolerance (Dot)", ClampMin=0, ClampMax=1,
			EditCondition="(Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyEqual || Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual) && Domain == EPCGUtilsDynMeshAngularDomain::Scalar", EditConditionHides))
	double DotTolerance = 0.1;

	/** Threshold as an angle. 0 same direction, 90 perpendicular, 180 opposite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, DisplayName="Degrees", ClampMin=0, ClampMax=180, Units="Degrees",
			EditCondition="ThresholdInput == EPCGUtilsDynMeshInputValueType::Constant && Domain == EPCGUtilsDynMeshAngularDomain::Degrees", EditConditionHides))
	double DegreesConstant = 25.0;

	/** How far from the threshold still counts as equal, for the Nearly Equal comparisons, as an angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, DisplayName="Tolerance (Degrees)", ClampMin=0, ClampMax=180, Units="Degrees",
			EditCondition="(Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyEqual || Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual) && Domain == EPCGUtilsDynMeshAngularDomain::Degrees", EditConditionHides))
	double DegreesTolerance = 5.0;

	/** The constant threshold as a dot product, whichever domain it was written in. */
	double GetConstantDot() const;

	/** Tolerance in the space Test compares in. */
	double GetComparisonTolerance() const;

	/** Whether a measured dot product passes against a threshold that is already a dot product. */
	bool Test(double Dot, double ThresholdDot) const;

	/**
	 * One threshold, as a dot product, for each of Count elements of Data. A constant threshold fills the array
	 * and needs no data. An attribute with a single value (a Data-domain attribute, say) is given to every element.
	 * Returns false and says why in OutError when the attribute cannot be read or holds a value outside the domain.
	 */
	bool ResolveThresholds(const UPCGData* Data, int32 Count, TArray<double>& OutThresholdDots, FText& OutError) const;

	void AddToCrc(FArchiveCrc32& Ar) const;
};
