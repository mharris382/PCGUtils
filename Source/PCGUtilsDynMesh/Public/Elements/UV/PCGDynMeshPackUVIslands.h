// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"
#include "GeometryScript/MeshUVFunctions.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshPackUVIslands.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshUVPackMethod : uint8
{
	Blender UMETA(DisplayName="Blender Style", ToolTip="Blender's Pack Islands with the Bounding Box shape method: average island scale, rotation, Scaled / Add / Fraction margins."),
	GeometryScript UMETA(DisplayName="Geometry Script Repack", ToolTip="Geometry Script's Repack UVs: a margin sized for a texture width, optional rotation.")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshUVPackRotation : uint8
{
	None UMETA(ToolTip="Islands keep their orientation."),
	Cardinal UMETA(ToolTip="Islands may be turned by 90 degrees where that packs tighter."),
	AxisAligned UMETA(DisplayName="Axis Aligned", ToolTip="Each island is first turned so its smallest bounding box is axis aligned."),
	Any UMETA(ToolTip="Axis Aligned, then turned by 90 degrees where that packs tighter (Blender's default).")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshUVPackMargin : uint8
{
	Scaled UMETA(ToolTip="Margin times the islands' overall UV size before packing, scaled with the islands (Blender's default)."),
	Add UMETA(ToolTip="Margin in the islands' own UV units, scaled with the islands."),
	Fraction UMETA(ToolTip="Margin is the exact gap between islands in the final UVs: 0.01 is a hundredth of the texture. For lightmaps, about 2 / resolution.")
};

/**
 * Packs the UV islands of a layer into the unit square without overlap, after Blender's Pack Islands (Shape Method:
 * Bounding Box; Concave and Convex shapes are not implemented). A DynMesh Selection or Selector packs only those
 * triangles' islands, into the whole square; other triangles keep their UVs. Islands are triangles that share UVs.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV pack islands layout repack blender margin rotate lightmap atlas selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshPackUVIslandsSettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPackUVIslands"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=0))
	EPCGUtilsDynMeshUVPackMethod Method = EPCGUtilsDynMeshUVPackMethod::Blender;

	/** Scale every island to the same UV area per unit of surface area first, so texel density is even. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::Blender", EditConditionHides))
	bool bAverageIslandScale = true;

	/** Scale the packed islands to fill the unit square. Off keeps their size, filling rows one unit wide upwards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable, DisplayName="Scale",
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::Blender", EditConditionHides))
	bool bScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::Blender", EditConditionHides))
	EPCGUtilsDynMeshUVPackRotation Rotation = EPCGUtilsDynMeshUVPackRotation::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::Blender", EditConditionHides))
	EPCGUtilsDynMeshUVPackMargin MarginMethod = EPCGUtilsDynMeshUVPackMargin::Scaled;

	/** Space between islands, read as Margin Method says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable, ClampMin=0, UIMin=0, UIMax=0.1,
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::Blender", EditConditionHides))
	double Margin = 0.001;

	/** Geometry Script Repack's settings: the margin is sized for Target Image Width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pack", meta=(PCG_Overridable,
		EditCondition="Method==EPCGUtilsDynMeshUVPackMethod::GeometryScript", EditConditionHides))
	FGeometryScriptRepackUVsOptions RepackOptions;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshPackUVIslandsElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshPackUVIslandsOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	EPCGUtilsDynMeshUVPackMethod Method = EPCGUtilsDynMeshUVPackMethod::Blender;
	bool bAverageIslandScale = true;
	bool bScale = true;
	EPCGUtilsDynMeshUVPackRotation Rotation = EPCGUtilsDynMeshUVPackRotation::Any;
	EPCGUtilsDynMeshUVPackMargin MarginMethod = EPCGUtilsDynMeshUVPackMargin::Scaled;
	double Margin = 0.001;
	FGeometryScriptRepackUVsOptions RepackOptions;

protected:
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
