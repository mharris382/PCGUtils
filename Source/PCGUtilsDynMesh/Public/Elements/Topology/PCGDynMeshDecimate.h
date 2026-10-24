// Copyright Max Harris

#pragma once

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
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh decimate collapse unsubdivide un-subdivide planar dissolve limited reduce triangles blender selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshDecimateSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
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
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable))
	EPCGUtilsDynMeshDecimateMode Mode = EPCGUtilsDynMeshDecimateMode::Collapse;

	/** Fraction of the triangles to keep. 1 keeps everything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	float Ratio = 0.5f;

	UPROPERTY(meta=(InlineEditConditionToggle))
	bool bUseWeightMap = false;

	/**
	 * Optional. Name of a vertex weight map (float, 0 to 1) that steers where Collapse removes detail, like
	 * Blender's vertex group. With a positive Weight Factor, higher weights keep more detail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, EditCondition="bUseWeightMap && Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FName WeightMapName = TEXT("DecimateWeight");

	/** How strongly the weight map steers Collapse. Negative inverts it (higher weights lose detail first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, UIMin="-3.0", UIMax="3.0",
		EditCondition="bUseWeightMap && Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	float WeightFactor = 1.0f;

	/** Geometry Script simplifier options used by Collapse. Its weight-map handles are replaced by Weight Map Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", AdvancedDisplay, meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Collapse", EditConditionHides))
	FGeometryScriptSimplifyMeshOptions CollapseOptions;

	/** How many times to un-subdivide. Each pass roughly quarters the quads of a subdivided grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, ClampMin="0", UIMax="10",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::UnSubdivide", EditConditionHides))
	int32 Iterations = 1;

	/** Faces whose normals differ by less than this merge into one. Degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="180.0", Units="Degrees",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	float AngleLimit = 5.0f;

	/** Remove every vertex that sits on the outline between exactly two merged faces, whatever its angle. Gives cleaner results with a high Angle Limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	bool bAllBoundaries = false;

	/** Do not merge faces with different material IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Material",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	bool bDelimitMaterial = true;

	/** Do not merge faces in different polygroups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Polygroup",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	bool bDelimitPolygroup = false;

	/** Do not merge across UV seams in any UV layer (Blender's Seam and UV delimits). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="UV Seams",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	bool bDelimitUVSeams = true;

	/** Do not merge across hard-normal edges (Blender's Sharp delimit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Decimate|Delimit", meta=(PCG_Overridable, DisplayName="Normal Seams",
		EditCondition="Mode==EPCGUtilsDynMeshDecimateMode::Planar", EditConditionHides))
	bool bDelimitNormalSeams = false;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshDecimateOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EPCGUtilsDynMeshDecimateMode Mode = EPCGUtilsDynMeshDecimateMode::Collapse;
	float Ratio = 0.5f;
	bool bUseWeightMap = false;
	FName WeightMapName;
	float WeightFactor = 1.0f;
	FGeometryScriptSimplifyMeshOptions CollapseOptions;
	int32 Iterations = 1;
	float AngleLimit = 5.0f;
	bool bAllBoundaries = false;
	bool bDelimitMaterial = true;
	bool bDelimitPolygroup = false;
	bool bDelimitUVSeams = true;
	bool bDelimitNormalSeams = false;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshDecimateElement final : public FPCGUtilsDynMeshProcessBaseElement
{
};
