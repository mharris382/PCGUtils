// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSurface.h"

#include "PCGGeometryCollectionSelectionFromDynMesh.generated.h"

class UPCGUtilsDynMeshSelectionFactoryData;

namespace PCGGeometryCollectionSelectionFromDynMeshConstants
{
	/** Takes a DynMesh Selector, so the pin carries the DynMesh family's own label. */
	inline const FName SelectorInputPin = TEXT("Selector");

	inline constexpr int32 PreconfiguredAny = 0;
	inline constexpr int32 PreconfiguredAll = 1;
}

/** How a piece's per-element results combine into one answer for its bone. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionMeshPredicateAggregation : uint8
{
	/** The bone is selected if at least one eligible element passes. */
	Any UMETA(DisplayName="Any Element"),

	/** The bone is selected only if every eligible element passes. */
	All UMETA(DisplayName="All Elements")
};

/**
 * Selects Geometry Collection bones by running a DynMesh selector over each piece's surface.
 *
 * This is the one place the two selection domains meet. It evaluates **pieces only** - every cluster, root,
 * embedded and geometry-less transform answers false - because a cluster's shape is just the union of the
 * pieces beneath it, and that aggregation is already expressible as `GC | Select | Parent` applied afterwards.
 * For the same reason there is no hierarchy-depth setting here: a predicate tests pieces, and a decorator
 * decides what to do with the answer. Composition through decorators is the whole point of the split.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionFromDynMeshFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	/** The DynMesh predicate, captured while the authoring node executed. */
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData> MeshSelector;

	UPROPERTY()
	EPCGGeometryCollectionMeshPredicateAggregation Aggregation =
		EPCGGeometryCollectionMeshPredicateAggregation::Any;

	UPROPERTY()
	EPCGUtilsGeometryCollectionSurfaceTarget SurfaceTarget = EPCGUtilsGeometryCollectionSurfaceTarget::All;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Bridges a DynMesh Selector into a Geometry Collection bone selection: "select the pieces whose surface
 * satisfies this mesh predicate".
 *
 * Every geometric DynMesh selector becomes a bone selector for free through this node, which is why the
 * Fracture module ships no bounds, normal, colour or occlusion selectors of its own.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="GC Geometry Collection Bone Piece Pieces Mesh Predicate Surface Adapter Bridge To Convert From Any All Interior Exterior"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionFromDynMeshSettings
	: public UPCGUtilsGeometryCollectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectionFromDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	/** Fully compact: the aggregation mode is the whole title, and the pins say what it operates on. */
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool ShouldShowCompactNodeTitle() const override { return true; }
	virtual bool CanUserEditTitle() const override { return false; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	/**
	 * Any selects a piece when one eligible element passes; All requires every one of them.
	 *
	 * A piece with no eligible elements is never selected, under either mode - an empty set is not a vacuous
	 * truth here, it means the predicate had nothing to say about that piece.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionMeshPredicateAggregation Aggregation =
		EPCGGeometryCollectionMeshPredicateAggregation::Any;

	/**
	 * Which of a piece's surface the predicate may look at.
	 *
	 * Exterior Only is surface inherited from the original mesh; Interior Only is surface a fracture cut
	 * created. This is what makes "the pieces whose *original* surface faces up" expressible, which is almost
	 * always what a predicate about an outside-facing surface means on a fractured solid. Hidden faces are
	 * never eligible under any setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsGeometryCollectionSurfaceTarget SurfaceTarget = EPCGUtilsGeometryCollectionSurfaceTarget::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
