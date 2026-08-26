#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDynamicMeshUVProcessBase.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/** Common settings for UV operations that accept either a Dynamic Mesh or a Dynamic Mesh Selection. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV")
class PCGUTILSDYNMESH_API UPCGDynamicMeshUVProcessBaseSettings
	: public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** Zero-based UV layer to create or update. Other UV layers are preserved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, ClampMin="0", UIMin="0"))
	int32 UVLayer = 0;

	virtual bool GetRequiredSelectionDomain(
		UE::Geometry::EGeometryElementType& OutElementType) const override;
};

/**
 * Resolves the effective triangle region and requested UV overlay before delegating to a UV operation.
 * Existing overlay triangles outside the region are never cleared or rebuilt by this base.
 */
class PCGUTILSDYNMESH_API FPCGDynamicMeshUVProcessBaseElement
	: public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	virtual bool ProcessMesh(UPCGDynamicMeshData* MeshData,
		const UPCGDynamicMeshSelectionData* SelectionData, FPCGContext* Context) const final;

	/** Called before mesh attributes or UV layers are created. Return false to pass this mesh through unchanged. */
	virtual bool ShouldProcessUVs(UPCGDynamicMeshData* MeshData,
		const UPCGDynamicMeshSelectionData* SelectionData,
		TArrayView<const int32> TriangleIDs, FPCGContext* Context) const
	{
		return true;
	}

	/** Applies the derived UV operation to the already-resolved triangle region. */
	virtual bool ProcessUVs(UPCGDynamicMeshData* MeshData,
		UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
		TArrayView<const int32> TriangleIDs,
		FPCGContext* Context) const = 0;
};
