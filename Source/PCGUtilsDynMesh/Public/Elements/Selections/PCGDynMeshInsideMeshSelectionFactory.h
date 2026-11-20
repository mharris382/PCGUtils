// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshInsideMeshSelectionFactory.generated.h"

class UPCGDynamicMeshData;

namespace PCGDynMeshInsideMeshSelectionFactoryConstants
{
	inline const FName VolumeInputPin = TEXT("Volume");
}

/**
 * How Select | Mesh Inclusion tests a mesh element against the Volume meshes. Mirrors the check types of PCGEx's
 * Filter : Inclusion (Dynamic Mesh), with each element's own shape standing in for a point's bounds.
 */
UENUM(BlueprintType)
enum class EPCGDynMeshInclusionCheck : uint8
{
	/** The element's center (vertex, edge midpoint, triangle centroid) is inside a Volume mesh. Expansion is ignored. */
	IsInside = 0 UMETA(DisplayName="Is Inside"),
	/** The element's center is outside, but the element (grown by Expansion) touches or crosses a Volume mesh's surface. */
	Intersects = 1 UMETA(DisplayName="Intersects"),
	/** The element's center is inside a Volume mesh, or the element (grown by Expansion) touches or crosses its surface. */
	IsInsideOrIntersects = 2 UMETA(DisplayName="Is Inside or Intersects"),
	/** The element's center is outside every Volume mesh, or the element (grown by Expansion) touches or crosses a surface. */
	IsOutsideOrIntersects = 3 UMETA(DisplayName="Is Outside or Intersects"),
	/** The element's center is inside a Volume mesh and the element (grown by Expansion) stays clear of its surface. */
	IsFullyInside = 4 UMETA(DisplayName="Is Fully Inside"),
};

/** Selection factory testing each mesh element for inclusion in the volume enclosed by other DynMeshes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshInsideMeshSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGDynamicMeshData>> VolumeMeshes;

	UPROPERTY()
	EPCGDynMeshInclusionCheck CheckType = EPCGDynMeshInclusionCheck::IsInside;

	UPROPERTY()
	FPCGUtilsInputShorthandDoubleAbs Expansion = FPCGUtilsInputShorthandDoubleAbs(0.0, TEXT("Expansion"));

	UPROPERTY()
	double WindingThreshold = 0.5;

	UPROPERTY()
	double Offset = 0.0;

	UPROPERTY()
	bool bMeshesAreActorLocal = true;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Selects the vertices, edges or triangles of a mesh by how they relate to the volume enclosed by the DynMeshes
 * on the Volume pin: inside it, crossing its surface, or both (see Check Type). The check types mirror PCGEx's
 * Filter : Inclusion (Dynamic Mesh). Invert Selection complements the result.
 *
 * Inside is decided by the generalized winding number at the element's center (the vertex, the edge midpoint or
 * the triangle centroid), so a volume mesh with small holes or overlapping parts still has a sensible inside.
 * Crossing uses the element's exact shape - the triangle itself, the edge segment, the vertex point - grown by
 * Expansion into a capsule of that radius: the element crosses when it comes within Expansion of a Volume
 * surface. Several Volume meshes act as their union.
 *
 * Offset grows (positive) or shrinks (negative) the volume by a distance from its surface for the inside test.
 * Use a small positive Offset to count elements lying on the volume's surface as inside, and a negative one to
 * keep only elements at least that deep inside, e.g. to pick the faces a boolean carved into the volume while
 * leaving the volume's own surface out.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select inclusion inside outside fully overlap overlapping intersect intersects intersection crosses contained enclosed volume winding other mesh"))
class PCGUTILSDYNMESH_API UPCGDynMeshInsideMeshSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshSelectionSourceSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshInsideMeshSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Which relation to the Volume meshes selects an element. Default Is Inside matches the node's original behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshInclusionCheck CheckType = EPCGDynMeshInclusionCheck::IsInside;

	/**
	 * Radius, in the Volume mesh's units, that each element is grown by before testing whether it crosses a Volume
	 * surface: an element within this distance of a surface counts as crossing it. 0 counts only elements that
	 * actually touch or pass through a surface. Must be 0 or greater. Ignored by Is Inside.
	 *
	 * In @Data mode it reads a numeric attribute (default name 'Expansion', optional) from the @Data domain of the
	 * tested mesh; a missing or negative value is an error.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, EditCondition="CheckType != EPCGDynMeshInclusionCheck::IsInside", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs Expansion = FPCGUtilsInputShorthandDoubleAbs(0.0, TEXT("Expansion"));

	/**
	 * Distance from the volume's surface, in the volume mesh's units, applied to the inside test. Positive also selects elements outside the
	 * volume within this distance of its surface; negative only selects elements inside by more than this
	 * distance. 0 is the volume as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, Units="cm"))
	double Offset = 0.0;

	/**
	 * Winding number above which a point counts as inside. 0.5 is right for closed meshes; lower it toward 0 to
	 * treat a badly open volume as more enclosing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="0.01", ClampMax="0.99", UIMin="0.01", UIMax="0.99"))
	double WindingThreshold = 0.5;

	/**
	 * Both meshes use the module's actor-local convention, so each is placed by its own target actor before the
	 * test. Disable when the tested mesh and the Volume meshes already share one coordinate space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bMeshesAreActorLocal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
};
