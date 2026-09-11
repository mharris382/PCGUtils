// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionDecorator.h"

#include "PCGGeometryCollectionSelectionHierarchy.generated.h"

/** How a hierarchy decorator rewrites the selection it is given. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionHierarchyOperation : uint8
{
	/** The cluster containing each selected bone. Roots contribute nothing. */
	Parent UMETA(DisplayName="Parent"),

	/** The direct children of each selected bone. A bone with no children keeps itself. */
	Children UMETA(DisplayName="Children"),

	/** Everything sharing a parent with a selected bone, the bone itself included. */
	Siblings UMETA(DisplayName="Siblings"),

	/** Every bone above each selected bone, up to the root. */
	Ancestors UMETA(DisplayName="Ancestors"),

	/** Every bone below each selected bone. */
	Descendants UMETA(DisplayName="Descendants"),

	/** The pieces that make up each selected bone's shape. A selected piece keeps itself. */
	ToPieces UMETA(DisplayName="To Pieces"),

	/** The cluster containing each selected bone, leaving already-selected clusters alone. */
	ToClusters UMETA(DisplayName="To Clusters"),

	/** Every bone at the same depth as any selected bone. */
	SameLevel UMETA(DisplayName="Same Level"),

	/** The ancestor of each selected bone at a chosen depth. */
	ToLevel UMETA(DisplayName="To Level"),

	/** Everything that was not selected. */
	Invert UMETA(DisplayName="Invert")
};

/** Which bones a Parent operation keeps when only some of a cluster's children were selected. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionParentMode : uint8
{
	/** Select a cluster if any of its children was selected. Matches Fracture Mode's Parent button. */
	AnyChild UMETA(DisplayName="Any Child Selected"),

	/** Select a cluster only if every one of its children was selected. */
	AllChildren UMETA(DisplayName="All Children Selected")
};

/** Which bones an Invert operation may produce. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionInvertDomain : uint8
{
	/** Every unselected transform, clusters and roots included. */
	AllBones UMETA(DisplayName="All Bones"),

	/** Only unselected pieces. Matches Fracture Mode, which inverts within the visible leaves. */
	PiecesOnly UMETA(DisplayName="Pieces Only")
};

/** Walks a Geometry Collection's hierarchy from an existing selection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionHierarchyFactoryData
	: public UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGGeometryCollectionHierarchyOperation Operation = EPCGGeometryCollectionHierarchyOperation::Parent;

	UPROPERTY()
	EPCGGeometryCollectionParentMode ParentMode = EPCGGeometryCollectionParentMode::AnyChild;

	UPROPERTY()
	EPCGGeometryCollectionInvertDomain InvertDomain = EPCGGeometryCollectionInvertDomain::AllBones;

	UPROPERTY()
	bool bPiecesOnly = false;

	UPROPERTY()
	int32 Level = 1;

protected:
	virtual bool TransformSelection(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		TArray<int32>& InOutBones) const override;

	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Moves a selection around the fracture hierarchy: up to parents, down to children, sideways to siblings.
 *
 * These are the operations behind Fracture Mode's Parent / Children / Siblings / Level buttons, expressed so
 * they compose. Because they take a selection and return a selection, a geometric selector never needs to know
 * anything about the hierarchy - "the clusters whose every piece is inside this volume" is a bounds selector
 * followed by Select Parent in All Children mode.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Selection Hierarchy Parent Children Siblings Ancestors Descendants Level Invert Cluster Piece Leaf"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectionHierarchySettings
	: public UPCGUtilsGeometryCollectionSelectionDecoratorSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectionHierarchy"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual bool ShouldDrawNodeCompact() const override
	{
		return Operation == EPCGGeometryCollectionHierarchyOperation::Invert;
	}
	virtual bool ShouldShowCompactNodeTitle() const override
	{
		return Operation == EPCGGeometryCollectionHierarchyOperation::Invert;
	}
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionHierarchyOperation Operation = EPCGGeometryCollectionHierarchyOperation::Parent;

	/**
	 * Whether one selected child is enough to select its cluster, or whether all of them must be.
	 *
	 * All Children Selected is what turns a per-piece test into a per-cluster one: run a geometric selector
	 * over the pieces, then keep only the clusters it matched completely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable,
			EditCondition="Operation == EPCGGeometryCollectionHierarchyOperation::Parent", EditConditionHides))
	EPCGGeometryCollectionParentMode ParentMode = EPCGGeometryCollectionParentMode::AnyChild;

	/** Keep only the pieces among the descendants, skipping the clusters between them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable,
			EditCondition="Operation == EPCGGeometryCollectionHierarchyOperation::Descendants", EditConditionHides))
	bool bPiecesOnly = false;

	/** Depth below the root: 0 is the root itself, 1 its children, and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0",
			EditCondition="Operation == EPCGGeometryCollectionHierarchyOperation::ToLevel", EditConditionHides))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable,
			EditCondition="Operation == EPCGGeometryCollectionHierarchyOperation::Invert", EditConditionHides))
	EPCGGeometryCollectionInvertDomain InvertDomain = EPCGGeometryCollectionInvertDomain::AllBones;

protected:
	virtual UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData* CreateDecoratorFactory(
		FPCGContext* InContext) const override;
};
