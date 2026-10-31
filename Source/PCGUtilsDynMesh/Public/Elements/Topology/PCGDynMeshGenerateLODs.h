// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshGenerateLODs.generated.h"

/** How the LODs' reductions are given. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshLODMode : uint8
{
	List UMETA(ToolTip="One entry per LOD, each with its own reduction."),
	Automatic UMETA(ToolTip="Set how many LODs, and how much LOD 0 is reduced. Each later LOD removes Reduction Per LOD of the triangles of the LOD before it.")
};

/** What a LOD entry's value means, in List mode. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshLODMetric : uint8
{
	DecimationRatio UMETA(DisplayName="Decimation Ratio", ToolTip="Fraction of the source mesh's triangles to remove, 0 to 1. 0 keeps the source as it is; 0.8 keeps a fifth of its triangles."),
	MaxTriangleCount UMETA(DisplayName="Max Triangle Count", ToolTip="The most triangles the LOD may have. A source with fewer is kept as it is."),
	Tolerance UMETA(ToolTip="How far, in cm, the LOD's surface may move from the source's. Removes triangles until any further collapse would move it more. 0 keeps the source as it is.")
};

/** What LOD 0's reduction is given as, in Automatic mode. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshLOD0Target : uint8
{
	DecimationRatio UMETA(DisplayName="Decimation Ratio", ToolTip="Fraction of the source mesh's triangles LOD 0 removes."),
	MaxTriangleCount UMETA(DisplayName="Max Triangle Count", ToolTip="The most triangles LOD 0 may have. The decimation ratio is worked out from each source's triangle count; a source with fewer is kept as it is.")
};

/** One LOD in List mode. */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshLODLevel
{
	GENERATED_BODY()

	FPCGUtilsDynMeshLODLevel() = default;
	FPCGUtilsDynMeshLODLevel(double InValue, FName InAttribute) : Value(InValue, InAttribute) {}

	/**
	 * This LOD's reduction, in the unit Metric names: a decimation ratio (0 to 1, the fraction of triangles removed),
	 * a max triangle count, or a tolerance in cm.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double, not negative) per input from that input's
	 * @Data domain. The @Data. prefix is optional. Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	FPCGUtilsInputShorthandDoubleAbs Value;
};

/**
 * Makes a set of LODs from each high-detail DynMesh in one node: for every input it outputs one decimated copy per LOD,
 * each marked with its LOD number on @Data, ready for Paint Vertex Color (Paint LODs From Highest Detail) and Save
 * Static Mesh (Save LODs From Attribute).
 *
 * Every LOD is decimated from the source, never from the LOD before it, so each reduction means exactly what it says.
 * LOD 0 may be reduced too, for a source that is denser than it needs to be.
 *
 * Every output is its input, reduced: it keeps the input's tags, @Data attributes, materials and target actor.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh LOD LODs level of detail generate decimate simplify reduce triangles ratio tolerance chain"))
class PCGUTILSDYNMESH_API UPCGDynMeshGenerateLODsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshGenerateLODsSettings();

#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshTopology);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshGenerateLODs"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Gives the shorthand members their plain override labels (LOD0DecimationRatio, LOD0DecimationRatioAttribute, ...). */
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable))
	EPCGUtilsDynMeshLODMode Mode = EPCGUtilsDynMeshLODMode::List;

	/** What each LOD entry's value means. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable,
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::List", EditConditionHides))
	EPCGUtilsDynMeshLODMetric Metric = EPCGUtilsDynMeshLODMetric::DecimationRatio;

	/**
	 * One entry per LOD, LOD 0 first. Each input gives one output per entry. Defaults to three LODs removing 0, 0.5
	 * and 0.8 of the triangles, read in @Data mode from LOD0Ratio, LOD1Ratio and LOD2Ratio. An entry added here
	 * defaults its attribute to LOD<n>Ratio.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::List", EditConditionHides))
	TArray<FPCGUtilsDynMeshLODLevel> LODs;

	/** How many LODs each input gives, LOD 0 included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, ClampMin=1, UIMin=1, UIMax=8,
		DisplayName="LOD Count", EditCondition="Mode==EPCGUtilsDynMeshLODMode::Automatic", EditConditionHides))
	int32 LODCount = 3;

	/** What LOD 0's reduction is given as. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, DisplayName="LOD 0 Target",
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::Automatic", EditConditionHides))
	EPCGUtilsDynMeshLOD0Target LOD0Target = EPCGUtilsDynMeshLOD0Target::DecimationRatio;

	/**
	 * Fraction of the source's triangles LOD 0 removes, 0 to 1. 0 keeps the source as LOD 0.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) between 0 and 1 per input from that input's
	 * @Data domain (default LOD0Ratio). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, DisplayName="LOD 0 Decimation Ratio",
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::Automatic && LOD0Target==EPCGUtilsDynMeshLOD0Target::DecimationRatio", EditConditionHides))
	FPCGUtilsInputShorthandDouble01 LOD0DecimationRatio;

	/**
	 * The most triangles LOD 0 may have. Its decimation ratio is worked out from each source's triangle count; a
	 * source with no more than this is kept as LOD 0.
	 *
	 * @Data mode reads one integer (Integer32 or Integer64, not negative) per input from that input's @Data domain
	 * (default LOD0MaxTriangles). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, DisplayName="LOD 0 Max Triangle Count",
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::Automatic && LOD0Target==EPCGUtilsDynMeshLOD0Target::MaxTriangleCount", EditConditionHides))
	FPCGUtilsInputShorthandInteger32Abs LOD0MaxTriangleCount;

	/**
	 * Fraction of the previous LOD's triangles each LOD after LOD 0 removes, 0 to 1. With 0.5, every LOD has half the
	 * triangles of the one before.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double) between 0 and 1 per input from that input's
	 * @Data domain (default LODReductionPerLOD). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, DisplayName="Reduction Per LOD",
		EditCondition="Mode==EPCGUtilsDynMeshLODMode::Automatic", EditConditionHides))
	FPCGUtilsInputShorthandDouble01 ReductionPerLOD;

	/**
	 * Geometry Script simplifier options every LOD is decimated with. Its weight-map handle is used as set. An engine
	 * options struct, so it stays a plain constant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", AdvancedDisplay, meta=(PCG_Overridable))
	FGeometryScriptSimplifyMeshOptions SimplifyOptions;

	/**
	 * Required output. @Data attribute (int32) receiving each output's LOD number, 0 for the most detailed. The High
	 * Quality Source output gets -1. Paint Vertex Color's and Save Static Mesh's LOD Attribute read it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName LODAttribute = TEXT("LOD");

	/** Write a value that groups each input's LODs to @Data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteGroupAttribute = true;

	/**
	 * Optional output. @Data attribute shared by the LODs of one input, so Paint Vertex Color (LOD Source Attribute)
	 * can group them. An input that already has this attribute keeps its value, so meshes from DynMesh | From Static
	 * Mesh keep their SourceMesh path. An input without it gets its index on In (int32).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, EditCondition="bWriteGroupAttribute"))
	FName GroupAttribute = TEXT("SourceMesh");

	/**
	 * Also output each input unchanged, before its LODs, so each input gives one more output than there are LODs.
	 * Its LOD Attribute is -1 and its High Quality Source Attribute true. Save Static Mesh (Save LODs From Attribute,
	 * Save High Quality Source From Attribute) writes it to the asset's Hi-Res Source Model in the same call as the
	 * LODs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, DisplayName="Output High Quality Source Mesh"))
	bool bOutputHighQualitySource = false;

	/**
	 * Required with Output High Quality Source Mesh. @Data attribute (bool) written on every output: true on the
	 * unchanged source, false on each LOD.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable, DisplayName="High Quality Source Attribute",
		EditCondition="bOutputHighQualitySource"))
	FName HighQualitySourceAttribute = TEXT("IsHighQualitySource");

	/**
	 * Decimate every LOD of every input one at a time instead of in parallel. For checking whether a problem comes
	 * from the parallel path; the output is the same either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Advanced", AdvancedDisplay, meta=(DisplayName="Disable Parallel"))
	bool bDisableParallel = false;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshGenerateLODsElement final : public IPCGElement
{
public:
	/** Allocates on the game thread, then decimates on worker threads it waits for. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
