// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGGeometryCollectionSelectBones.generated.h"

/** Which bones a GC Select Bones node picks, with no input selection to transform. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionBoneSelectionMode : uint8
{
	/** Every transform in the collection, clusters and roots included. */
	All UMETA(DisplayName="All"),

	/** Nothing. Useful as a neutral input while building a graph up. */
	None UMETA(DisplayName="None"),

	/** Bones with no parent. A collection from DynMesh To GC has exactly one. */
	Root UMETA(DisplayName="Root"),

	/** Fracture pieces: rigid bones that own geometry. What converts, renders and prunes. */
	Pieces UMETA(DisplayName="Pieces"),

	/** Structural cluster bones. Their shape is the union of the pieces beneath them. */
	Clusters UMETA(DisplayName="Clusters"),

	/** Every bone at exactly one depth below the root. */
	AtLevel UMETA(DisplayName="At Level")
};

/** Selects bones by their role in the collection, without reference to any other selection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectBonesFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGGeometryCollectionBoneSelectionMode Mode = EPCGGeometryCollectionBoneSelectionMode::Pieces;

	UPROPERTY()
	int32 Level = 1;

	UPROPERTY()
	bool bExcludeEmbedded = true;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * The starting point of a selection graph: pick bones by what they are.
 *
 * Everything here mirrors a Fracture Mode selection button, and every one of them is a *base* selector - it
 * takes no Selection input. Feed the result to the hierarchy decorators to walk the tree from there, or to
 * Selection Logic to combine it with a geometric selector.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Select Bones All None Root Leaf Pieces Clusters Level"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectBonesSettings
	: public UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectBones"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionBoneSelectionMode Mode = EPCGGeometryCollectionBoneSelectionMode::Pieces;

	/** Depth below the root: 0 is the root itself, 1 its children, and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0",
			EditCondition="Mode == EPCGGeometryCollectionBoneSelectionMode::AtLevel", EditConditionHides))
	int32 Level = 1;

	/**
	 * Skip embedded bones, which reference an exemplar mesh rather than owning geometry.
	 *
	 * Nothing in this module creates one, so this only matters for a collection that came from elsewhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay,
		meta=(PCG_Overridable,
			EditCondition="Mode == EPCGGeometryCollectionBoneSelectionMode::AtLevel", EditConditionHides))
	bool bExcludeEmbedded = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
