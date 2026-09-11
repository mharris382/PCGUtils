// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGGeometryCollectionSelectionLogic.generated.h"

namespace PCGGeometryCollectionSelectionLogicConstants
{
	/** The set every other selection is combined into. */
	inline const FName SelectionAInputPin = TEXT("A");

	/** The set combined with A. Multiple connections here are unioned first. */
	inline const FName SelectionBInputPin = TEXT("B");
}

/** How two GC selections combine. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionSelectionLogicMode : uint8
{
	/** In A and in B. */
	And UMETA(DisplayName="AND"),

	/** In A or in B. */
	Or UMETA(DisplayName="OR"),

	/** In one but not both. */
	Xor UMETA(DisplayName="XOR"),

	/** In A but not in B. */
	Subtract UMETA(DisplayName="Subtract")
};

/** Combines two GC selections with a set operation. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionLogicFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGGeometryCollectionSelectionLogicMode Mode = EPCGGeometryCollectionSelectionLogicMode::And;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> FactoriesA;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> FactoriesB;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Set algebra over bone selections.
 *
 * Connecting several selectors to one Selection pin anywhere else already means their union, so this node
 * exists for everything else: intersection, difference, and exclusive or. Difference is the one that comes up
 * most - "the exposed pieces, except the ones I already damaged".
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Selection Logic Boolean And Or Xor Subtract Difference Intersect Union Combine"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionLogicSettings
	: public UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectionLogic"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool ShouldShowCompactNodeTitle() const override { return true; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionSelectionLogicMode Mode = EPCGGeometryCollectionSelectionLogicMode::And;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
