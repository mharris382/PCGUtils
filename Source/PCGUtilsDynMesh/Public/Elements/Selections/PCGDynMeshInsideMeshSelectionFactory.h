// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshInsideMeshSelectionFactory.generated.h"

class UPCGDynamicMeshData;

namespace PCGDynMeshInsideMeshSelectionFactoryConstants
{
	inline const FName VolumeInputPin = TEXT("Volume");
}

/** Selection factory testing whether each mesh element lies inside the volume enclosed by other DynMeshes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshInsideMeshSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGDynamicMeshData>> VolumeMeshes;

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
 * Selects the vertices, edges or triangles of a mesh that lie inside the volume enclosed by the DynMeshes on
 * the Volume pin. Turn on Invert Selection to select what lies outside instead.
 *
 * Inside is decided by the generalized winding number, so a volume mesh with small holes or overlapping parts
 * still has a sensible inside. An element is tested at its center: the vertex, the edge midpoint or the
 * triangle centroid. Several Volume meshes act as their union.
 *
 * Offset grows (positive) or shrinks (negative) the volume by a distance from its surface. Use a small
 * positive Offset to count elements lying on the volume's surface as inside, and a negative one to keep only
 * elements at least that deep inside, e.g. to pick the faces a boolean carved into the volume while leaving
 * the volume's own surface out.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select inside outside overlap overlapping intersect intersection contained enclosed volume winding other mesh"))
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

	/**
	 * Distance from the volume's surface, in the volume mesh's units. Positive also selects elements outside the
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
