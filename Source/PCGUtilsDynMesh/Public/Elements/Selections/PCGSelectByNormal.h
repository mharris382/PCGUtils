#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"

#include "PCGSelectByNormal.generated.h"

UENUM(BlueprintType)
enum class EPCGDynMeshNormalSelectionElementType : uint8
{
	Triangle,
	Vertex
};

/**
 * Filters a Dynamic Mesh element selection (or all mesh triangles/vertices, if none is supplied) by comparing each
 * element's normal against a reference direction, keeping elements whose normal is at least Dot Threshold aligned
 * with it (Dot(ElementNormal, ReferenceDirection) >= Dot Threshold). Both the reference direction and element
 * normals are evaluated in the Dynamic Mesh's own local/vertex space.
 *
 * Triangle normals are the geometric face normal (Mesh.GetTriNormal). Vertex normals prefer the mesh's normal
 * overlay when present, then a baked per-vertex normal, matching the fallback chain used elsewhere in this module
 * (eg Dynamic Mesh Selection To Points, Dynamic Mesh Sample).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectByNormalSettings : public UPCGDynamicMeshSelectionFilterBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectByNormal"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Type of mesh element to select. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshNormalSelectionElementType ElementType = EPCGDynMeshNormalSelectionElementType::Triangle;

	/** Reference direction that element normals are compared against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FVector ReferenceDirection = FVector(0.0, 0.0, 1.0);

	/** Minimum Dot(ElementNormal, ReferenceDirection) for an element to be selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="-1.0", ClampMax="1.0"))
	float DotThreshold = 0.9f;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectByNormalElement : public FPCGDynamicMeshSelectionFilterBaseElement
{
protected:
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};
