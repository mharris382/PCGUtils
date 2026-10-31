// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGSelectSelfOcclusion.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "PCGUtilsDynMeshNodeColor.h"

#include "PCGSelectMeshOcclusion.generated.h"

class UPCGDynamicMeshData;

namespace PCGSelectMeshOcclusionConstants
{
	inline const FName OccludersPin = TEXT("Occluders");
}

/** Selection factory: whether a ray from each element hits the DynMeshes on the Occluders pin. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectMeshOcclusionFactoryData : public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGDynamicMeshData>> OccluderMeshes;
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
	bool bIncludeSelf = false;
	UPROPERTY()
	bool bIgnoreSourceTriangles = true;
	UPROPERTY()
	bool bMeshesAreActorLocal = true;

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

/**
 * Selects DynMesh vertices or triangles by whether a ray fired from each one hits the DynMeshes on the Occluders
 * pin: Select | By Self Occlusion, with other meshes doing the blocking. Include Self adds the tested mesh as an
 * occluder too.
 *
 * Each occluder is queried directly through its own spatial index; no collision or world trace is involved.
 * An incoming selection is converted to the sample type and only those candidates are traced, so a cheap Selector
 * such as Select | By Normal makes a good pruning pass first.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select occlusion occluded occluder raycast ray trace shadow cover sheltered exposed visible other mesh"))
class PCGUTILSDYNMESH_API UPCGSelectMeshOcclusionSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSelectMeshOcclusionSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Vertex;
		bSupportsMaterializedElementTypeOverride = false;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectMeshOcclusion"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual FString GetAdditionalTitleInformation() const override;
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

	/** World: Trace Direction is a world direction. Mesh Local: it turns with the tested mesh's target actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable))
	EPCGDynMeshSelfOcclusionDirectionSpace DirectionSpace = EPCGDynMeshSelfOcclusionDirectionSpace::World;

	/** Maximum ray length in world units (the shared space when Meshes Are Actor Local is off). Zero means unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	double MaximumDistance = 0.0;

	/** Also let the tested mesh block its own rays, as Select | By Self Occlusion does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable))
	bool bIncludeSelf = false;

	/** Moves the ray origin along the element normal, so an occluder touching the surface is not hit at distance zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention", meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	double NormalOffset = 0.1;

	/** Moves the ray origin forward along Trace Direction, for the same reason. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention", meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	double DirectionOffset = 0.1;

	/** With Include Self, skip the source triangle (or every triangle around the source vertex) for its own ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Hit Prevention",
		meta=(PCG_Overridable, EditCondition="bIncludeSelf"))
	bool bIgnoreSourceTriangles = true;

	/**
	 * The tested mesh and every occluder use the module's actor-local convention, so each is placed by its own
	 * target actor before tracing. Disable when they already share one coordinate space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bMeshesAreActorLocal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
	virtual UE::Geometry::EGeometryElementType GetMaterializedElementType() const override
	{
		return ElementType == EPCGDynMeshSelfOcclusionElementType::Vertex
			? UE::Geometry::EGeometryElementType::Vertex
			: UE::Geometry::EGeometryElementType::Face;
	}
};
