#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

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
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectSelfOcclusionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGDynMeshSelfOcclusionElementType ElementType = EPCGDynMeshSelfOcclusionElementType::Vertex;
	UPROPERTY()
	EPCGDynMeshSelfOcclusionResult Result = EPCGDynMeshSelfOcclusionResult::Occluded;
	UPROPERTY()
	FVector TraceDirection = FVector::UpVector;
	UPROPERTY()
	EPCGDynMeshSelfOcclusionDirectionSpace DirectionSpace = EPCGDynMeshSelfOcclusionDirectionSpace::World;
	UPROPERTY()
	double MaximumDistance = 0.0;
	UPROPERTY()
	double NormalOffset = 0.1;
	UPROPERTY()
	double DirectionOffset = 0.1;
	UPROPERTY()
	bool bIgnoreSourceTriangles = true;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex
			? UE::Geometry::EGeometryElementType::Vertex
			: UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Self Raycast Mesh Occlusion Selection Selector DynMesh"))
class PCGUTILSDYNMESH_API UPCGSelectSelfOcclusionSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSelectSelfOcclusionSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Vertex;
		bSupportsMaterializedElementTypeOverride = false;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectSelfOcclusion"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Native mesh element sampled at its position (vertex) or centroid (triangle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, DisplayName="Occlusion Sample Type"))
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual UE::Geometry::EGeometryElementType GetMaterializedElementType() const override
	{
		return ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex
			? UE::Geometry::EGeometryElementType::Vertex
			: UE::Geometry::EGeometryElementType::Face;
	}
};
