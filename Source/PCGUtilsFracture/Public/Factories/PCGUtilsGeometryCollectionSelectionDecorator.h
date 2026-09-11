// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGUtilsGeometryCollectionSelectionDecorator.generated.h"

/**
 * A selector that transforms the result of other selectors instead of selecting from scratch.
 *
 * This is the split that keeps the selector layer small: a geometric predicate answers "which pieces match"
 * and knows nothing about the hierarchy, while a decorator answers "given those bones, which bones do I
 * actually want" and knows nothing about geometry. Parent, Children, Siblings, Level and Contact are all the
 * second kind, and every one of them is a thin wrapper over
 * GeometryCollection::Facades::FCollectionTransformSelectionFacade.
 *
 * The alternative - giving every geometric selector its own bone-depth setting - was rejected during the
 * architecture investigation: it puts hierarchy handling in every predicate, and it still cannot express
 * "clusters all of whose pieces matched" versus "clusters where any piece matched", which composition can.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	/** Selectors whose union forms this decorator's input, captured when the node ran. */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> ChildFactories;

	/** Union the result with the input rather than replacing it. */
	UPROPERTY()
	bool bIncludeOriginal = false;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const final;

protected:
	/**
	 * Rewrites the incoming bone list into the outgoing one. Implementations replace the contents; the base
	 * applies bIncludeOriginal afterwards, so an implementation never has to think about it.
	 *
	 * @return false to fail the whole selection (log first).
	 */
	virtual bool TransformSelection(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		TArray<int32>& InOutBones) const
		PURE_VIRTUAL(UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData::TransformSelection, return false;);

	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Provider base for decorator nodes: one required Selection input pin, one Selection output pin.
 *
 * Derived settings supply the node's identity and CreateDecoratorFactory; everything else is here so a new
 * hierarchy operation stays the twenty lines it should be.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionSelectionDecoratorSettings
	: public UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/**
	 * Keep the incoming bones in the result as well.
	 *
	 * Off matches Fracture Mode, where each selection button replaces the selection. Turn it on to grow a
	 * selection instead - "these pieces and their children", "these bones and everything above them".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bIncludeOriginal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	/** Creates the concrete decorator. Children and bIncludeOriginal are filled in by the base afterwards. */
	virtual UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData* CreateDecoratorFactory(
		FPCGContext* InContext) const
		PURE_VIRTUAL(UPCGUtilsGeometryCollectionSelectionDecoratorSettings::CreateDecoratorFactory, return nullptr;);

	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
