// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionDecorator.h"

#include "PCGGeometryCollectionSelectContact.generated.h"

/** Expands a bone selection to the bones physically touching it. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectContactFactoryData
	: public UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bIncludeNeighborsInParentLevels = true;

	UPROPERTY()
	int32 Iterations = 1;

protected:
	virtual bool TransformSelection(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		TArray<int32>& InOutBones) const override;

	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Selects the fracture pieces adjacent to the ones already selected - Fracture Mode's Contact button.
 *
 * Adjacency is the engine's *precise* proximity: bones sharing vertices, or touching coplanar opposite-facing
 * triangles, which is exactly the shape a fracture cut produces. Because proximity is only ever computed
 * between pieces, selecting the contacts of a cluster gives neighbouring clusters rather than the pieces
 * inside them.
 *
 * This is the expensive selector in the family: proximity is recomputed from the collection's geometry every
 * time. For spreading a selection with rules of your own - weighted by contact area, or by distance - emit the
 * adjacency graph with GC Bones To Points instead and flood fill it with PCGEx.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Select Contact Neighbors Neighbours Adjacent Touching Proximity Grow"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectContactSettings
	: public UPCGUtilsGeometryCollectionSelectionDecoratorSettings
{
	GENERATED_BODY()

public:
	UPCGGeometryCollectionSelectContactSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectContact"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * Also select a touching bone that sits nearer the root than the bones being queried.
	 *
	 * Only matters in a hierarchy of mixed depth - a piece touching a cluster's worth of geometry. Matches the
	 * engine's own default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bIncludeNeighborsInParentLevels = true;

	/**
	 * How far to spread: 1 is the touching bones, 2 also their neighbours, and so on.
	 *
	 * Proximity is computed once and reused across iterations, so raising this is far cheaper than chaining
	 * several of these nodes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="1"))
	int32 Iterations = 1;

protected:
	virtual UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData* CreateDecoratorFactory(
		FPCGContext* InContext) const override;
};
