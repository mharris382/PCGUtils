#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"

#include "PCGSelectSelfOcclusion.generated.h"

UENUM(BlueprintType)
enum class EPCGDynMeshSelfOcclusionElementType : uint8
{
	Vertex,
	Triangle
};

UENUM(BlueprintType)
enum class EPCGDynMeshSelfOcclusionDirectionSpace : uint8
{
	/** Trace Direction is already expressed in the Dynamic Mesh's coordinate space. */
	MeshLocal,
	/** Trace Direction is expressed in world space and is converted using the PCG target actor transform. */
	World
};

UENUM(BlueprintType)
enum class EPCGDynMeshSelfOcclusionResult : uint8
{
	/** Select candidate elements whose ray hits another part of the mesh. */
	Occluded,
	/** Select candidate elements whose ray reaches Maximum Distance without hitting the mesh. */
	Unoccluded
};

/**
 * Selects Dynamic Mesh vertices or triangles according to whether a ray fired from each element hits another part
 * of the same mesh. The mesh is queried directly through one FDynamicMeshAABBTree3 built per input; no collision
 * component or world trace is involved.
 *
 * When an existing Dynamic Mesh Selection is supplied, it is converted to Element Type using GeometryScript's
 * inclusive conversion rules and only those candidates are traced. This makes a cheap selector such as Select by
 * Normal an effective pruning pass before the more expensive self-occlusion queries.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectSelfOcclusionSettings : public UPCGDynamicMeshSelectionFilterBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectSelfOcclusion"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Mesh element sampled at its position (vertex) or centroid (triangle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshSelfOcclusionElementType ElementType = EPCGDynMeshSelfOcclusionElementType::Vertex;

	/** Whether the output contains blocked candidates or candidates with a clear ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshSelfOcclusionResult Result = EPCGDynMeshSelfOcclusionResult::Occluded;

	/** Direction from each element toward the light or visibility source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable))
	FVector TraceDirection = FVector::UpVector;

	/** Coordinate space in which Trace Direction is expressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable))
	EPCGDynMeshSelfOcclusionDirectionSpace DirectionSpace = EPCGDynMeshSelfOcclusionDirectionSpace::World;

	/** Maximum ray length in mesh-local units. Zero means unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable, ClampMin="0.0"))
	double MaximumDistance = 0.0;

	/** Moves the ray origin along the element normal to avoid numerical self-intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention", meta=(PCG_Overridable, ClampMin="0.0"))
	double NormalOffset = 0.1;

	/** Moves the ray origin forward along Trace Direction to avoid zero-distance intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention", meta=(PCG_Overridable, ClampMin="0.0"))
	double DirectionOffset = 0.1;

	/** Ignore the source triangle, or every triangle incident to the source vertex, during its ray query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention", meta=(PCG_Overridable))
	bool bIgnoreSourceTriangles = true;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectSelfOcclusionElement : public FPCGDynamicMeshSelectionFilterBaseElement
{
public:
	/** World-space direction conversion resolves the PCG target actor; mesh access also follows the module's main-thread convention. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};
