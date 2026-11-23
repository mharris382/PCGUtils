// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPointConversion.h"
#include "Factories/PCGUtilsFactoryProvider.h"
#include "Factories/PCGUtilsGeometryCollectionPointConversion.h"
#include "Selectors/PCGVoxelSelector.h"

#include "PCGUtilsPointSelectorProvider.generated.h"

class UPCGUtilsPointSelectorData;

/** What a Selector | node selects, and so which kind of selector comes out of it. Saved in graphs: append, never reorder. */
UENUM(BlueprintType)
enum class EPCGUtilsSelectorDomain : uint8
{
	/** Voxel cells. The output is a Voxel Selector, for the Selector pin of Voxel | Pattern. */
	Voxel = 0,
	/** DynMesh vertices, edges or triangles. The output is a DynMesh Selector, for any DynMesh Selector pin. */
	DynMesh = 1 UMETA(DisplayName="DynMesh"),
	/** Geometry Collection pieces. The output is a GC Selection, for any GC Selection pin. */
	GC = 2 UMETA(DisplayName="GC"),
};

/**
 * Base of every Selector | node: a node that makes a point selector. Domain decides what the node selects and the
 * type of its output pin, which is always that domain's own selector type; the node owns no pin type.
 *
 * A subclass only says how to make its point selector (CreatePointSelector) and adds its own pins and settings.
 * It contains no per-domain code. The base wraps the point selector for the chosen domain, shows that domain's
 * conversion settings, and puts one palette entry per domain in the menu: "Selector | <Name> (Voxel)",
 * "(DynMesh)" and "(GC)". An entry only sets Domain, which can be changed afterwards.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Selectors", PrioritizeCategories="Domain")
class PCGUTILSSELECTORS_API UPCGUtilsPointSelectorProviderSettings : public UPCGUtilsFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override;
#endif
	/** The domain, under the node title. */
	virtual FString GetAdditionalTitleInformation() const override;

	/** The output pin is named as the chosen domain's own selector nodes name theirs: Selector (Voxel, DynMesh) or Selection (GC). */
	virtual FName GetMainOutputPin() const override;

	/**
	 * What this node selects: Voxel cells, DynMesh elements or GC pieces. It decides the type of the output pin (a
	 * Voxel Selector, a DynMesh Selector or a GC Selection) and which conversion settings are shown. Changing it
	 * changes the output pin, so existing wires from it are dropped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain")
	EPCGUtilsSelectorDomain Domain = EPCGUtilsSelectorDomain::Voxel;

	/** How a voxel cell is shown to the selector as a point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain",
		meta=(PCG_Overridable, DisplayName="Voxel Conversion", EditCondition="Domain == EPCGUtilsSelectorDomain::Voxel", EditConditionHides))
	FPCGVoxelPointConversion VoxelConversion;

	/** How DynMesh vertices, edges and triangles are shown to the selector as points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain",
		meta=(PCG_Overridable, DisplayName="DynMesh Conversion", EditCondition="Domain == EPCGUtilsSelectorDomain::DynMesh", EditConditionHides))
	FPCGUtilsDynMeshPointConversion DynMeshConversion;

	/** How GC pieces are shown to the selector as points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain",
		meta=(PCG_Overridable, DisplayName="GC Conversion", EditCondition="Domain == EPCGUtilsSelectorDomain::GC", EditConditionHides))
	FPCGUtilsGeometryCollectionPointConversion GCConversion;

	/** Select everything the selector does not, among the same candidates (cells; mesh elements; GC pieces, never clusters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	bool bInvert = false;

	/**
	 * Passed on to the domain's selector as its own Priority, and it means what that domain makes of it. It never
	 * changes what is selected. DynMesh: where several selectors share a pin, the one with the higher
	 * Priority is tested first. Voxel and GC: several selectors on a pin select what any of them selects, so
	 * Priority has no effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

protected:
	/**
	 * The one thing a subclass implements: makes this node's point selector from its settings and input pins. Null
	 * (already logged on the graph) emits nothing. The base calls Prepare on it, so do not.
	 */
	virtual UPCGUtilsPointSelectorData* CreatePointSelector(FPCGContext* InContext) const
		PURE_VIRTUAL(UPCGUtilsPointSelectorProviderSettings::CreatePointSelector, return nullptr;);

	virtual UPCGUtilsFactoryData* CreateFactoryBase(FPCGContext* InContext) const override final;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override final;
};
