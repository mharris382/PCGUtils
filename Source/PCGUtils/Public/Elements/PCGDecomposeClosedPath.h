#pragma once

#include "PCGSettings.h"
#include "PCGElement.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGDecomposeClosedPath.generated.h"

/** Experimental 3D boundary decomposition. Each input is an ordered closed point path. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils", meta=(Keywords="Path Decompose Closed PCGEx"))
class PCGUTILS_API UPCGDecomposeClosedPathSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DecomposeClosedPath"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Paths); }
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif
	/** Weight of triangle area after normalization by median world-space boundary edge length. Must be finite and nonnegative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Triangulation", meta=(PCG_Overridable, ClampMin="0"))
	FPCGUtilsInputShorthandDouble AreaWeight {1.0};

	/** Weight of triangle perimeter after normalization. At least one triangulation weight must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Triangulation", meta=(PCG_Overridable, ClampMin="0"))
	FPCGUtilsInputShorthandDouble LengthWeight {0.1};

	/** Prefer horizontal regions (equal world Z), retaining their boundary with slopes. Halves the seed cost of level triangles; does not move points. Off preserves the original algorithm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Triangulation", meta=(PCG_Overridable))
	bool bBiasFlatCuts = false;

	/** Maximum 1 - projected area / convex hull area when merging patches. Lower values retain more cuts around bends. Range [0,1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merging", meta=(PCG_Overridable, ClampMin="0", ClampMax="0.99"))
	FPCGUtilsInputShorthandDouble MaxConcavity {0.10};

	/** Maximum plane deviation divided by (2 * projected area / perimeter) for patches with up to four vertices. Allows twisted quads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merging", meta=(PCG_Overridable, ClampMin="0"))
	FPCGUtilsInputShorthandDouble MaxQuadWarp {0.35};

	/** Maximum normalized plane deviation for patches with more than four vertices. Keeps larger patches nearly planar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merging", meta=(PCG_Overridable, ClampMin="0"))
	FPCGUtilsInputShorthandDouble MaxPolygonWarp {0.02};

	/** Forward larger inputs unchanged with a warning instead of running the cubic triangulation search. Supported range 3..2048 points per input path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Limits", meta=(PCG_Overridable, ClampMin="3", ClampMax="2048"))
	FPCGUtilsInputShorthandInteger32 MaxInputPoints {2048};

	/** Preserve input metadata in all domains, including point entries and @Data. No vertices are added; entries follow their exact source vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metadata", meta=(PCG_Overridable))
	bool bInheritMetadata = true;

/** Required input/output bool in the Data domain. Must be true on input; outputs are closed. Use a bare attribute name (default IsClosed). */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(PCG_Overridable))
 FName ClosedAttributeName = TEXT("IsClosed");

 /** Write an optional point-domain bool identifying points whose outgoing edge is an interior cut. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
 bool bOutputIsCut = false;

 /** Optional output bool in the point domain. True when this point starts an interior cut edge; the final point's edge wraps to the first. Overwrites an existing attribute of the same name. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputIsCut", EditConditionHides))
 FName IsCutAttributeName = TEXT("IsCut");

protected:
 virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGDecomposeClosedPathElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
