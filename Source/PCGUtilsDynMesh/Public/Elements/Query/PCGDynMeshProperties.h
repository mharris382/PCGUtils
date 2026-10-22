// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Geometry/PCGUtilsDynMeshProperties.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshProperties.generated.h"

/**
 * Writes properties of each mesh to its @Data domain: counts and layer layout that are free to read, and
 * measurements that need a pass over the mesh (bounds, area, volume, pieces, triangle quality, normal problems).
 * The mesh itself passes through unchanged.
 *
 * Whole mesh only. The properties always describe the entire mesh; an incoming selection passes through untouched
 * and does not narrow them. Topology nodes such as Simplify can write the free properties themselves, inline.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Query", HideCategories=(Selector),
	meta=(Keywords="DynMesh mesh properties info inspect stats statistics count triangles vertices edges materials polygroups bounds area volume quality normals data"))
class PCGUTILSDYNMESH_API UPCGDynMeshPropertiesSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPropertiesSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshProperties"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	/** Properties that cost nothing to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Free", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshFreeProperties Free;

	/** Properties that need one pass over the mesh. Nothing is computed for a property that is off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Computed", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshComputedProperties Computed;

	virtual const FPCGUtilsDynMeshFreeProperties* GetInlineFreeProperties() const override { return &Free; }
	virtual const FPCGUtilsDynMeshComputedProperties* GetInlineComputedProperties() const override { return &Computed; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshPropertiesElement final : public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	/** Nothing to change: the base writes the enabled properties after this returns. */
	virtual bool ProcessMesh(UPCGDynamicMeshData*, const UPCGDynamicMeshSelectionData*, FPCGContext*) const override
	{
		return true;
	}
};
