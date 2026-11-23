// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsPointSelectorProvider.h"
#include "Selectors/PCGUtilsInBoundsPointSelector.h"

#include "PCGSelectorInBounds.generated.h"

namespace PCGSelectorInBoundsPins
{
	inline const FName BoundsLabel = TEXT("Bounds");
}

/**
 * Selector | In Bounds: selects what lies in the bounds of any point on the Bounds pin. In the palette as
 * Selector | In Bounds (Voxel), (DynMesh) and (GC); Domain on the node says which, and so what comes out.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Selectors", PrioritizeCategories="Domain",
	meta=(Keywords="Selector Select Bounds Box Inside"))
class PCGUTILSSELECTORS_API UPCGSelectorInBoundsSettings : public UPCGUtilsPointSelectorProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectorInBounds"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * How a point that has bounds is tested against a bound: Center (its position is inside), Any Part (its box
	 * touches the bound) or Fully Inside (its whole box is inside one bound). A point with no bounds is only a
	 * position, so all three are then the same test. Whether a point has bounds is set by the domain's
	 * Conversion (for example a voxel cell as Center With Bounds, or a DynMesh triangle as its three Vertices).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	EPCGUtilsSelectorContainment Containment = EPCGUtilsSelectorContainment::Center;

	/** Grows (or, negative, shrinks) every bound on all sides before testing, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	double Expand = 0.0;

protected:
	virtual UPCGUtilsPointSelectorData* CreatePointSelector(FPCGContext* InContext) const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
