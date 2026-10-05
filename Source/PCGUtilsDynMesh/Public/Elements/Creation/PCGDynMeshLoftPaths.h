// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGDynMeshLoftPaths.generated.h"

namespace PCGDynMeshLoftPathsConstants
{
	const FName PathAInputPin = TEXT("Path A");
	const FName PathBInputPin = TEXT("Path B");
}

/** Where the loft's second rail comes from. */
UENUM(BlueprintType)
enum class EPCGUtilsLoftSecondRail : uint8
{
	/** Rail B is Path A offset sideways and along the up axis, one point for one point. */
	Offset,
	/** Rail B is read from the Path B pin, one path per Path A path. */
	PathB UMETA(DisplayName = "Path B")
};

/** How Path B's points are paired with Path A's. */
UENUM(BlueprintType)
enum class EPCGUtilsLoftCorrespondence : uint8
{
	/** Path B is resampled by arc length to Path A's point count. Path B's own points are not preserved. */
	ArcLength UMETA(DisplayName = "Arc Length"),
	/** Point N of Path A pairs with point N of Path B, both used exactly as given. The counts must match. */
	Index
};

/** How height changes across the loft, from Rail A to Rail B. */
UENUM(BlueprintType)
enum class EPCGUtilsLoftProfile : uint8
{
	/** A straight ramp between the rails. */
	Linear,
	/** Level where it meets both rails, steepest in the middle. */
	Smooth,
	/** Level where it meets Rail A, steepening toward Rail B: a roll-off. */
	LevelAtA UMETA(DisplayName = "Level at A"),
	/** Steepest at Rail A, levelling out where it meets Rail B. */
	LevelAtB UMETA(DisplayName = "Level at B"),
	/** Custom Profile curve: X is 0 at Rail A and 1 at Rail B, Y is the fraction of the height difference. */
	Custom
};

/** Whether the paths are treated as closed loops. */
UENUM(BlueprintType)
enum class EPCGUtilsLoftClosure : uint8
{
	/** Read from Path A's closed-loop data attribute; a path without it is open. */
	FromAttribute UMETA(DisplayName = "From Attribute"),
	Open,
	Closed
};

/**
 * Builds a structured DynMesh surface between two paths: one column of quads per path point and a set number of
 * rows across, with the height of each row following a profile.
 *
 * Path points are used exactly as given, never resampled (except Path B under Arc Length correspondence). A loft
 * whose path was read off a mesh boundary therefore lands its edge vertices on that boundary's vertices, so the
 * two can be welded.
 *
 * This is a creation node: it has no DynMesh input, so there is no selection to honour.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|DynMesh|Creation",
	meta=(Keywords="DynMesh mesh loft path paths rail rails strip ruled surface slope ramp bank create"))
class PCGUTILSDYNMESH_API UPCGDynMeshLoftPathsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshCreation);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshLoftPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	// ── Rails ─────────────────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails", meta = (PCG_Overridable))
	EPCGUtilsLoftSecondRail SecondRail = EPCGUtilsLoftSecondRail::Offset;

	/** Sideways offset of Rail B from Path A, perpendicular to the up axis. Negate it to offset to the other side of an open path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	double OffsetDistance = 100.0;

	/** Offset of Rail B from Path A along the up axis. Negative puts Rail B below Path A. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	double OffsetHeight = -50.0;

	/** For a closed path, a positive Offset Distance always points away from the enclosed area, whichever way the path winds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	bool bPositiveOffsetIsOutward = true;

	/**
	 * Averages each point's offset direction over this multiple of the offset distance along the path, rounding
	 * corners. This is what keeps a densely sampled path, such as a remeshed mesh boundary, from folding the loft
	 * over itself at corners. 1 handles corners up to a right angle; sharper corners need more. 0 offsets each
	 * point along its mitered corner instead, which keeps corners sharp but only suits sparse paths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, ClampMin = "0.0", EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	double OffsetSmoothing = 1.0;

	/** Used when Offset Smoothing is 0. Caps how far a corner's offset may exceed Offset Distance, as a multiple of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, ClampMin = "1.0", EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	double MiterLimit = 2.0;

	/** Read Offset Distance per path from a Path A data attribute, so one node can loft paths of different widths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails|Per-Path Attributes",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	bool bOffsetDistanceFromAttribute = false;

	/** Optional @Data-domain numeric attribute on Path A (double, float or integer). A path without it uses Offset Distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails|Per-Path Attributes",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset && bOffsetDistanceFromAttribute", EditConditionHides))
	FName OffsetDistanceAttributeName = TEXT("LoftOffsetDistance");

	/** Read Offset Height per path from a Path A data attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails|Per-Path Attributes",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset", EditConditionHides))
	bool bOffsetHeightFromAttribute = false;

	/** Optional @Data-domain numeric attribute on Path A (double, float or integer). A path without it uses Offset Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails|Per-Path Attributes",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::Offset && bOffsetHeightFromAttribute", EditConditionHides))
	FName OffsetHeightAttributeName = TEXT("LoftOffsetHeight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, EditCondition = "SecondRail==EPCGUtilsLoftSecondRail::PathB", EditConditionHides))
	EPCGUtilsLoftCorrespondence Correspondence = EPCGUtilsLoftCorrespondence::ArcLength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails", meta = (PCG_Overridable))
	EPCGUtilsLoftClosure Closure = EPCGUtilsLoftClosure::FromAttribute;

	/**
	 * Optional @Data-domain Boolean attribute on Path A marking it as a closed loop. Matches the attribute written
	 * by DynMesh | Selection To Paths. A path that does not carry it is treated as open.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rails",
		meta = (PCG_Overridable, EditCondition = "Closure==EPCGUtilsLoftClosure::FromAttribute", EditConditionHides))
	FName IsClosedAttributeName = TEXT("IsClosed");

	// ── Surface ───────────────────────────────────────────────────────────────

	/** Quad rows between the rails. More rows follow a curved profile more closely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable, ClampMin = "1"))
	int32 Rows = 8;

	/** Read Rows per path from a Path A data attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Per-Path Attributes", meta = (PCG_Overridable))
	bool bRowsFromAttribute = false;

	/** Optional @Data-domain numeric attribute on Path A (integer, or a float that is rounded). A path without it, or with a value below 1, uses Rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Per-Path Attributes",
		meta = (PCG_Overridable, EditCondition = "bRowsFromAttribute", EditConditionHides))
	FName RowsAttributeName = TEXT("LoftRows");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable))
	EPCGUtilsLoftProfile Profile = EPCGUtilsLoftProfile::Smooth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface",
		meta = (EditCondition = "Profile==EPCGUtilsLoftProfile::Custom", EditConditionHides))
	FRuntimeFloatCurve CustomProfile;

	/** Direction height is measured along, in the output mesh's space. Everything perpendicular to it is interpolated linearly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable))
	FVector UpAxis = FVector::UpVector;

	/** Faces point along the up axis by default; this reverses them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable))
	bool bFlipFaces = false;

	/**
	 * Closed lofts only: fill Rail B with a flat cap that shares its vertices with the loft, in its own PolyGroup.
	 * Turns an inward loft into a plateau or bowl with no second seam. The cap is a polygon fill with no interior
	 * vertices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable))
	bool bCapRailB = false;

	/** UV units per world unit. U runs along Path A, V across the loft. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (PCG_Overridable, ClampMin = "0.0"))
	double UVScale = 0.01;

	// ── Output ────────────────────────────────────────────────────────────────

	/** Converts world-space path points into the PCG target actor's local space, the space DynMesh data is expected to be in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	bool bConvertWorldToActorLocal = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshLoftPathsElement : public IPCGElement
{
public:
	/** Resolving the target actor for the local-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
