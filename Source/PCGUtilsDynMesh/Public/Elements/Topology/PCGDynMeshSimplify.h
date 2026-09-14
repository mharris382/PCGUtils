// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshSimplifyFunctions.h"

#include "PCGDynMeshSimplify.generated.h"

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

/** Exposes Geometry Script's mesh simplification library as a selection-aware DynMesh process. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh simplify planar decimate reduce triangles vertices polygroup cluster selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshSimplifySettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSimplify"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify", meta=(PCG_Overridable))
	EPCGDynMeshSimplifyMode Mode = EPCGDynMeshSimplifyMode::Planar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynMeshSimplifyMode::Planar", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptPlanarSimplifyOptions PlanarOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynMeshSimplifyMode::PolygroupTopology", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptPolygroupSimplifyOptions PolygroupOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynMeshSimplifyMode::PolygroupTopology", EditConditionHides))
	FGeometryScriptGroupLayer GroupLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target", meta=(PCG_Overridable, ClampMin="1",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::TriangleCount", EditConditionHides))
	int32 TargetTriangleCount = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target", meta=(PCG_Overridable, ClampMin="1",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::VertexCount", EditConditionHides))
	int32 TargetVertexCount = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target", meta=(PCG_Overridable, ClampMin="0.0",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::Tolerance", EditConditionHides))
	float GeometricTolerance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target", meta=(PCG_Overridable, ClampMin="0.0",
		EditCondition="Mode==EPCGDynMeshSimplifyMode::EdgeLength || Mode==EPCGDynMeshSimplifyMode::ClusterEdgeLength", EditConditionHides))
	double TargetEdgeLength = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynMeshSimplifyMode::TriangleCount || Mode==EPCGDynMeshSimplifyMode::VertexCount || Mode==EPCGDynMeshSimplifyMode::Tolerance || Mode==EPCGDynMeshSimplifyMode::EdgeLength", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptSimplifyMeshOptions SimplifyOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simplify",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynMeshSimplifyMode::ClusterEdgeLength", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptClusterSimplifyMeshOptions ClusterOptions;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSimplifyOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EPCGDynMeshSimplifyMode Mode = EPCGDynMeshSimplifyMode::Planar;
	FGeometryScriptPlanarSimplifyOptions PlanarOptions;
	FGeometryScriptPolygroupSimplifyOptions PolygroupOptions;
	FGeometryScriptGroupLayer GroupLayer;
	int32 TargetTriangleCount = 100;
	int32 TargetVertexCount = 100;
	float GeometricTolerance = 1.0f;
	double TargetEdgeLength = 10.0;
	FGeometryScriptSimplifyMeshOptions SimplifyOptions;
	FGeometryScriptClusterSimplifyMeshOptions ClusterOptions;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshSimplifyElement final : public FPCGUtilsDynMeshProcessBaseElement
{
};
