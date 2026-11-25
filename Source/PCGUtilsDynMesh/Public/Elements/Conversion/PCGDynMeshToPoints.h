#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshToPoints.generated.h"

namespace PCGDynMeshToPointsConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
}

/** Which mesh element DynMesh | To Points makes a point for. */
UENUM(BlueprintType)
enum class EPCGDynMeshToPointsMode : uint8
{
	/** One point per vertex, in vertex ID order. */
	Vertices,
	/** One point per triangle, at its centroid, in triangle ID order. */
	Triangles
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh to points vertices triangles faces centroid convert"))
class PCGUTILSDYNMESH_API UPCGDynMeshToPointsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshConversion);
	}
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title;	}
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
		return true;
	}
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DynMesh|Point")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "DynMeshToPoints_Title", "DynMesh | To Points");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "DynMeshToPoints_Tooltip",
			"Converts a Dynamic Mesh to PCG points. Vertices mode makes one point per vertex in vertex ID order, with the vertex's position and color. "
			"Triangles mode makes one point per triangle in triangle ID order, at the centroid, facing the triangle's normal, with the average color of its corners; "
			"its Triangle ID attribute is what Select | From Triangle IDs reads. Point Z follows the normal; point X follows the tangent when available, otherwise rotation is derived from the normal alone.");
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/** Vertices (default) makes one point per vertex. Triangles makes one point per triangle, at its centroid and facing its normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	EPCGDynMeshToPointsMode Mode = EPCGDynMeshToPointsMode::Vertices;

	/** Transform local mesh positions, normals, and tangents into the PCG target actor's world space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/** Vertices mode. Add the source dynamic-mesh vertex ID to every generated point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable,
		EditCondition = "Mode == EPCGDynMeshToPointsMode::Vertices", EditConditionHides))
	bool bOutputVertexIndex = false;

	/** Attribute (int32) that receives the source dynamic-mesh vertex ID. Optional; written only in Vertices mode with Output Vertex Index on. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable,
		EditCondition = "Mode == EPCGDynMeshToPointsMode::Vertices && bOutputVertexIndex", EditConditionHides))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/** Triangles mode. Add the source dynamic-mesh triangle ID to every generated point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable,
		EditCondition = "Mode == EPCGDynMeshToPointsMode::Triangles", EditConditionHides))
	bool bOutputTriangleId = false;

	/**
	 * Attribute (int32) that receives the source dynamic-mesh triangle ID. Optional; written only in Triangles mode
	 * with Output Triangle Id on. The default matches the attribute Select | From Triangle IDs reads.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable,
		EditCondition = "Mode == EPCGDynMeshToPointsMode::Triangles && bOutputTriangleId", EditConditionHides))
	FName TriangleIdAttribute = TEXT("TriangleId");

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGDynMeshToPointsContext : public FPCGContext {};

class FPCGDynMeshToPointsElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
