// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetGeometryCollectionData.generated.h"

class UGeometryCollectionComponent;

/**
 * Finds UGeometryCollectionComponents on the actors resolved by the inherited Get Actor Data actor/component
 * selection and pulls each one's rest collection into the graph as GC data - one data per component.
 *
 * The world-space counterpart of GC | From Asset. Where that node reads an asset and knows nothing about where
 * it sits, this one reads placed components and keeps their placement, which is what lets world-space PCG data
 * (a bounds selection, a point scatter, a trace result) line up with the geometry.
 *
 * Read-only: the components, their assets and their materials are never created, modified, hidden,
 * attached/detached or destroyed. Each component's collection is deep-copied, so nothing downstream can write
 * back to the level.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Get Actor Component World Level Placed Existing Source GC Geometry Collection Data Import"))
class PCGUTILSFRACTURE_API UPCGGetGeometryCollectionDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetGeometryCollectionDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetGCData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
	virtual FLinearColor GetNodeTitleColor() const override;
#endif

	//~Begin UPCGDataFromActorSettings interface
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Any; }

protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
	//~End UPCGDataFromActorSettings interface

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * Re-expresses each component's placement relative to the PCG target actor, instead of discarding it.
	 *
	 * Leave this on. GC data follows the DynMesh convention that the canonical space is the PCG actor's local
	 * space, and world-space PCG data is converted into it on the way in - so a component's world placement has
	 * to be *carried*, not dropped, or a bounds selection authored in the level would resolve against geometry
	 * sitting somewhere else. Turning it off imports each collection at the origin, which is only what you want
	 * when the placement is genuinely irrelevant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bConvertWorldToActorLocal = true;

	/** Carry each component's material list onto its GC data, respecting component-level overrides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bExtractMaterials = true;

	/**
	 * Keep the geometry of cluster bones whose faces are all invisible.
	 *
	 * A collection fractured in Fracture Mode carries a full hidden copy of each pre-fracture shape on the bone
	 * that was cut. Nothing downstream can use it and every later operation carries it along, so it is discarded
	 * by default - the same choice GC | Fracture and GC | From Asset make.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", AdvancedDisplay, meta=(PCG_Overridable))
	bool bKeepHiddenGeometry = false;
};

class PCGUTILSFRACTURE_API FPCGGetGeometryCollectionDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
