#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Elements/Selections/PCGDynMeshBoundsSelectionTypes.h"

#include "PCGSelectInPointBounds.generated.h"

namespace PCGSelectInPointBoundsConstants
{
	inline const FName PointsInputPin = TEXT("Points");
}

/**
 * Selects Dynamic Mesh elements whose test position(s) fall inside the oriented bounding box of one or more PCG
 * points (point Transform + BoundsMin/BoundsMax). Point rotation is respected - a rotated marker defines a rotated
 * selection volume, not a world-aligned AABB. Multiple points union their regions together.
 *
 * If fed an existing Dynamic Mesh Selection (rather than a bare Dynamic Mesh), the result is intersected with it,
 * so this node can either seed a new selection from a mesh or narrow down an already-filtered one.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectInPointBoundsSettings : public UPCGDynamicMeshSelectionFilterBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectInPointBounds"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Type of mesh element to select. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshBoundsSelectionElementType ElementType = EPCGDynMeshBoundsSelectionElementType::Edge;

	/** How an element's vertices are tested against each point's oriented bounds. Vertices are always tested by exact position, so this is ignored for Vertex elements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="ElementType!=EPCGDynMeshBoundsSelectionElementType::Vertex", EditConditionHides))
	EPCGDynMeshBoundsTestMode BoundsTestMode = EPCGDynMeshBoundsTestMode::ElementCenterInside;

	/**
	 * Converts each point's oriented bounds into the PCG target actor's local space before testing, matching the
	 * coordinate space Dynamic Mesh vertices are expected to be in by default (see Selection From Spline,
	 * PCGDynMeshActorSpaceTransform's ToWorld/ToLocal). PCG points are always authored in world space, so this
	 * should stay enabled unless the Dynamic Mesh input has already been converted to world space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bConvertPointsToLocalSpace = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectInPointBoundsElement : public FPCGDynamicMeshSelectionFilterBaseElement
{
public:
	/** Resolving the target actor for point/mesh coordinate-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};
