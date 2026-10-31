// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"
#include "GeometryScript/MeshUVFunctions.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshAutoUV.generated.h"

/** The Geometry Script unwrap methods Auto UV offers. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshAutoUVMethod : uint8
{
	PatchBuilder UMETA(DisplayName="Patch Builder", ToolTip="Grows patches over the surface, merges them by curvature and angle, flattens each with an exponential map, and packs them. Good general default."),
	XAtlas UMETA(DisplayName="XAtlas", ToolTip="The XAtlas library: charts by normal and flatness, then packs them. Often fewer seams than Patch Builder on hard-surface meshes."),
	Conformal UMETA(DisplayName="Conformal (Per Island)", ToolTip="Keeps the existing islands (UV islands or polygroups) and flattens each one conformally (least squares). Islands are not packed unless Pack Islands is on."),
	SpectralConformal UMETA(DisplayName="Spectral Conformal (Per Island)", ToolTip="Keeps the existing islands and flattens each with spectral conformal mapping, which needs no pinned vertices and distorts large islands less."),
	ExpMap UMETA(DisplayName="Exponential Map (Per Island)", ToolTip="Keeps the existing islands and flattens each with a discrete exponential map, which is fast and suits fairly flat islands.")
};

/**
 * Generates UVs with one of Geometry Script's automatic methods. The method chosen shows under the node title.
 * A DynMesh Selection or Selector unwraps only those triangles: they are unwrapped on their own (packed into
 * their own unit square), and every other triangle keeps its UVs.
 *
 * The methods are Geometry Script's, so quality is theirs: fine for lightmaps and baking, not hand-unwrap quality.
 * For a lightmap channel, set UV Layer to 1 and follow with Pack UV Islands if the margin needs control.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV auto unwrap generate lightmap patch builder xatlas conformal spectral exponential map selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshAutoUVSettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshAutoUV"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	/** The unwrap method. Shown under the node title. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=0))
	EPCGUtilsDynMeshAutoUVMethod Method = EPCGUtilsDynMeshAutoUVMethod::PatchBuilder;

	/** Patch Builder's settings, as Geometry Script names them. Auto Pack packs the patches into the unit square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Patch Builder", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshAutoUVMethod::PatchBuilder", EditConditionHides))
	FGeometryScriptPatchBuilderOptions PatchBuilderOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XAtlas", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshAutoUVMethod::XAtlas", EditConditionHides))
	FGeometryScriptXAtlasOptions XAtlasOptions;

	/**
	 * What the per-island methods treat as an island. PolyGroups works on any mesh; UV Islands needs the triangles to
	 * have UVs already, and keeps their seams.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method!=EPCGUtilsDynMeshAutoUVMethod::PatchBuilder && Method!=EPCGUtilsDynMeshAutoUVMethod::XAtlas", EditConditionHides))
	EGeometryScriptUVIslandSource IslandSource = EGeometryScriptUVIslandSource::PolyGroups;

	/** The polygroup layer that defines islands when Island Source is PolyGroups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method!=EPCGUtilsDynMeshAutoUVMethod::PatchBuilder && Method!=EPCGUtilsDynMeshAutoUVMethod::XAtlas", EditConditionHides))
	FGeometryScriptGroupLayer GroupLayer;

	/** Turn each flattened island so its longest extent runs along U or V. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method!=EPCGUtilsDynMeshAutoUVMethod::PatchBuilder && Method!=EPCGUtilsDynMeshAutoUVMethod::XAtlas", EditConditionHides))
	bool bAutoAlignIslandsWithAxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshAutoUVMethod::ExpMap", EditConditionHides))
	FGeometryScriptExpMapUVOptions ExpMapOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshAutoUVMethod::SpectralConformal", EditConditionHides))
	FGeometryScriptSpectralConformalUVOptions SpectralConformalOptions;

	/**
	 * Pack the flattened islands into the unit square afterwards, as Pack UV Islands does with its defaults (Average
	 * Island Scale, Rotation Any, Scaled margin 0.001). Off leaves the islands overlapping where they were flattened.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Per Island", meta=(PCG_Overridable,
		EditCondition="Method!=EPCGUtilsDynMeshAutoUVMethod::PatchBuilder && Method!=EPCGUtilsDynMeshAutoUVMethod::XAtlas", EditConditionHides))
	bool bPackIslands = true;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;

	/** Writes a UV overlay only, so a Builder's active selection survives it. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshAutoUVElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshAutoUVOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	EPCGUtilsDynMeshAutoUVMethod Method = EPCGUtilsDynMeshAutoUVMethod::PatchBuilder;
	FGeometryScriptPatchBuilderOptions PatchBuilderOptions;
	FGeometryScriptXAtlasOptions XAtlasOptions;
	FGeometryScriptRecomputeUVsOptions RecomputeOptions;
	bool bPackIslands = true;

protected:
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
