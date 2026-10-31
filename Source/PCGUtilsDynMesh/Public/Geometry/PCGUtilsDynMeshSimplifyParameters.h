// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/MeshSimplifyFunctions.h"

#include "PCGUtilsDynMeshSimplifyParameters.generated.h"

class UDynamicMesh;

UENUM(BlueprintType)
enum class EPCGDynMeshSimplifyMode : uint8
{
	Planar,
	PolygroupTopology UMETA(DisplayName="Polygroup Topology"),
	TriangleCount UMETA(DisplayName="Triangle Count"),
	VertexCount UMETA(DisplayName="Vertex Count"),
	Tolerance,
	EdgeLength UMETA(DisplayName="Edge Length"),
	ClusterEdgeLength UMETA(DisplayName="Cluster Edge Length")
};

/**
 * One Geometry Script simplification, as a value. Used outside a PCG graph by brush components that simplify
 * their mesh once and cache it; the DynMesh | Simplify node runs the same code through ApplySimplify().
 */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGDynMeshSimplifyParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify")
	EPCGDynMeshSimplifyMode Mode = EPCGDynMeshSimplifyMode::TriangleCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(EditCondition="Mode==EPCGDynMeshSimplifyMode::Planar", EditConditionHides))
	FGeometryScriptPlanarSimplifyOptions PlanarOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(EditCondition="Mode==EPCGDynMeshSimplifyMode::PolygroupTopology", EditConditionHides))
	FGeometryScriptPolygroupSimplifyOptions PolygroupOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(EditCondition="Mode==EPCGDynMeshSimplifyMode::PolygroupTopology", EditConditionHides))
	FGeometryScriptGroupLayer GroupLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify", meta=(ClampMin="1",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::TriangleCount", EditConditionHides))
	int32 TargetTriangleCount = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify", meta=(ClampMin="1",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::VertexCount", EditConditionHides))
	int32 TargetVertexCount = 250;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify", meta=(ClampMin="0.0",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::Tolerance", EditConditionHides))
	float GeometricTolerance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify", meta=(ClampMin="0.0",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::EdgeLength || Mode==EPCGDynMeshSimplifyMode::ClusterEdgeLength", EditConditionHides))
	double TargetEdgeLength = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(EditCondition="Mode==EPCGDynMeshSimplifyMode::TriangleCount || Mode==EPCGDynMeshSimplifyMode::VertexCount || Mode==EPCGDynMeshSimplifyMode::Tolerance || Mode==EPCGDynMeshSimplifyMode::EdgeLength", EditConditionHides))
	FGeometryScriptSimplifyMeshOptions SimplifyOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(EditCondition="Mode==EPCGDynMeshSimplifyMode::ClusterEdgeLength", EditConditionHides))
	FGeometryScriptClusterSimplifyMeshOptions ClusterOptions;

	/** True when the selected mode's options ask Geometry Script to compact the mesh afterwards. */
	bool WantsAutoCompact() const;
};

namespace PCGUtilsDynMeshSimplify
{
	/**
	 * Runs the simplification on Mesh. bSuppressAutoCompact turns off each mode's own compaction, for callers that
	 * still need the pre-simplify element IDs (region restoration) and compact afterwards themselves.
	 */
	PCGUTILSDYNMESH_API void ApplySimplify(UDynamicMesh* Mesh, const FPCGDynMeshSimplifyParameters& Parameters,
		bool bSuppressAutoCompact = false);

	PCGUTILSDYNMESH_API FText ModeText(EPCGDynMeshSimplifyMode Mode);
}
