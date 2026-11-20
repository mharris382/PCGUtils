// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshNormalsFunctions.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshComputeTangents.generated.h"

/**
 * Computes tangents (tangent and bitangent overlays) for the whole Dynamic Mesh from its normals and a UV layer,
 * with GeometryScript `ComputeTangents` (MikkT by default, as a static mesh build does).
 *
 * Run it after the last operation that merges meshes or changes triangles and before the mesh is spawned. A
 * spawned Dynamic Mesh Component uses the mesh's own tangents; triangles that arrived without tangents (from a
 * boolean, an append, or a mesh built in code) otherwise light the material's normal map through a different
 * frame and show a lighting seam that Unlit does not.
 *
 * Always processes the whole mesh: tangents are smoothed across neighbouring triangles, so computing them on a
 * part would leave a seam at its edge. A Selector, if connected, is ignored.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh tangent tangents bitangent binormal mikkt normal map lighting seam"))
class PCGUTILSDYNMESH_API UPCGDynMeshComputeTangentsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshComputeTangents"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	/**
	 * How tangents are computed. Fast MikkT (default) matches a static mesh's MikkT tangents closely; Standard MikkT
	 * is the exact algorithm (slower, falls back to Fast MikkT where unavailable); Per Triangle gives each triangle
	 * its own unsmoothed frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tangents", meta=(PCG_Overridable))
	EGeometryScriptTangentTypes TangentType = EGeometryScriptTangentTypes::FastMikkT;

	/** The UV layer the tangents follow: the layer the material samples its normal map with (usually 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tangents", meta=(PCG_Overridable, ClampMin="0", ClampMax="7"))
	int32 UVLayer = 0;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGDynMeshComputeTangentsElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshComputeTangentsOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	EGeometryScriptTangentTypes TangentType = EGeometryScriptTangentTypes::FastMikkT;
	int32 UVLayer = 0;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
