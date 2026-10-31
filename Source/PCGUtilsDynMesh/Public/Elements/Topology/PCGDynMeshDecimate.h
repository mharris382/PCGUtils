// Copyright Max Harris

#pragma once

#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Geometry/PCGUtilsDynMeshProperties.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "PCGUtilsDynMeshNodeColor.h"

#include "PCGDynMeshDecimate.generated.h"

/** The three modes of Blender's Decimate modifier. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshDecimateMode : uint8
{
	Collapse UMETA(ToolTip="Edge collapse to a fraction of the triangles, keeping the overall shape (quadric error)."),
	UnSubdivide UMETA(DisplayName="Un-Subdivide", ToolTip="Reverses grid subdivision: each pass merges every 2x2 block of quads into one quad."),
	Planar UMETA(ToolTip="Merges faces that are flat within the angle limit into single faces, then retriangulates them.")
};

/**
 * Blender's Decimate modifier for DynMesh: Collapse, Un-Subdivide and Planar. Written from Blender's documented
 * behaviour, not its source. A DynMesh Selection or Selector limits which triangles change.
 *
 * Every setting except Mode and Collapse Options is an input shorthand: a constant, or one value per input read from
 * that input's @Data domain. Only the settings the chosen Mode uses are read. Assets saved before the shorthands
 * load their old plain values into the Constant.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh decimate collapse unsubdivide un-subdivide planar dissolve limited reduce triangles blender selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshDecimateSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshDecimateSettings();

	/** Mesh properties written to @Data after this node runs, as DynMesh | Properties writes them. All off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output Properties", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties OutputProperties;

	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &OutputProperties; }

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshDecimate"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable))
	EPCGUtilsDynMeshDecimateMode Mode = EPCGUtilsDynMeshDecimateMode::Collapse;

	/**
	 * Fraction of the triangles to keep, 0 to 1. 1 keeps everything.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) between 0 and 1 per input from the input's
	 * @Data domain. The @Data. prefix is optional on DynMesh data. Required on every input when Mode is Collapse;
	 * the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FPCGUtilsInputShorthandDouble01 Ratio;

	/**
	 * Steer Collapse with the vertex weight map named by Weight Map Name, like Blender's vertex group.
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Collapse; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bUseWeightMap;

	/**
	 * Name of a vertex weight map (float, 0 to 1) that steers where Collapse removes detail. With a positive Weight
	 * Factor, higher weights keep more detail. Read only when Use Weight Map is on.
	 *
	 * @Data mode reads one Name or String per input from the input's @Data domain, naming a weight map on that
	 * input's mesh. Required on every input that uses a weight map; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FPCGUtilsInputShorthandName WeightMapName;

	/**
	 * How strongly the weight map steers Collapse. Negative inverts it (higher weights lose detail first). Read only
	 * when Use Weight Map is on.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) per input from the input's @Data domain.
	 * Required on every input that uses a weight map; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FPCGUtilsInputShorthandDouble WeightFactor;

	/** Geometry Script simplifier options used by Collapse. Its weight-map handles are replaced by Weight Map Name. An engine options struct, so it stays a plain constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", AdvancedDisplay, meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FGeometryScriptSimplifyMeshOptions CollapseOptions;

	/**
	 * How many times to un-subdivide. Each pass roughly quarters the quads of a subdivided grid.
	 *
	 * @Data mode reads one integer (Integer32 or Integer64, not negative) per input from the input's @Data domain.
	 * Required on every input when Mode is Un-Subdivide; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, UIMax="10",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::UnSubdivide", EditConditionHides))
	FPCGUtilsInputShorthandInteger32Abs Iterations;

	/**
	 * Faces whose normals differ by less than this merge into one. Degrees, 0 to 180.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) per input from the input's @Data domain,
	 * between 0 and 180. Required on every input when Mode is Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs AngleLimit;

	/**
	 * Remove every vertex that sits on the outline between exactly two merged faces, whatever its angle. Gives cleaner
	 * results with a high Angle Limit.
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bAllBoundaries;

	/**
	 * Do not merge faces with different material IDs.
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Material",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bDelimitMaterial;

	/**
	 * Do not merge faces in different polygroups.
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Polygroup",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bDelimitPolygroup;

	/**
	 * Do not merge across UV seams in any UV layer (Blender's Seam and UV delimits).
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="UV Seams",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bDelimitUVSeams;

	/**
	 * Do not merge across hard-normal edges (Blender's Sharp delimit).
	 *
	 * @Data mode reads one Boolean per input from the input's @Data domain. Required on every input when Mode is
	 * Planar; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Normal Seams",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	FPCGUtilsInputShorthandBoolean bDelimitNormalSeams;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;

	/** A Builder carries no @Data until realized, so @Data-driven settings only run on realized meshes. */
	virtual bool SupportsDeferredBuilderProcessing() const override;
	virtual FText GetBuilderInputRejectionReason() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshDecimateOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EPCGUtilsDynMeshDecimateMode Mode = EPCGUtilsDynMeshDecimateMode::Collapse;
	FPCGUtilsInputShorthandDouble01 Ratio;
	FPCGUtilsInputShorthandBoolean bUseWeightMap;
	FPCGUtilsInputShorthandName WeightMapName;
	FPCGUtilsInputShorthandDouble WeightFactor;
	FGeometryScriptSimplifyMeshOptions CollapseOptions;
	FPCGUtilsInputShorthandInteger32Abs Iterations;
	FPCGUtilsInputShorthandDoubleAbs AngleLimit;
	FPCGUtilsInputShorthandBoolean bAllBoundaries;
	FPCGUtilsInputShorthandBoolean bDelimitMaterial;
	FPCGUtilsInputShorthandBoolean bDelimitPolygroup;
	FPCGUtilsInputShorthandBoolean bDelimitUVSeams;
	FPCGUtilsInputShorthandBoolean bDelimitNormalSeams;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;

private:
	/** The settings after every shorthand the Mode uses was resolved for one input. Operations are shared, so this is per call. */
	struct FResolved
	{
		float Ratio = 0.5f;
		bool bUseWeightMap = false;
		FName WeightMapName;
		float WeightFactor = 1.0f;
		int32 Iterations = 1;
		float AngleLimit = 5.0f;
		bool bAllBoundaries = false;
		bool bDelimitMaterial = true;
		bool bDelimitPolygroup = false;
		bool bDelimitUVSeams = true;
		bool bDelimitNormalSeams = false;
	};

	bool Resolve(const FPCGUtilsDynMeshProcessInvocation& Invocation, FResolved& Out) const;
	bool ExecuteResolved(const FPCGUtilsDynMeshProcessInvocation& Invocation, const FResolved& R,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const;
};

class PCGUTILSDYNMESH_API FPCGDynMeshDecimateElement final : public FPCGUtilsDynMeshProcessBaseElement
{
};
