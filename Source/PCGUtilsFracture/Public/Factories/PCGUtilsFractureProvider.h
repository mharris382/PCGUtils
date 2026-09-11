// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGUtilsFractureProvider.generated.h"

class FGeometryCollection;
class UPCGUtilsFractureFactoryData;

namespace PCGUtilsFractureProviderConstants
{
	/** Output pin on GC | Fracture: a reusable Selection of every bone the execution created. */
	inline const FName ResultOutputPin = TEXT("Result");
}

/**
 * Selects the bones one GC | Fracture execution created. Bone IDs, rather than transform indices, keep the
 * selector valid when a later edit reindexes the same collection lineage.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGUtilsFractureResultSelectionFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	/** Collection lineage this result belongs to. */
	UPROPERTY()
	FGuid CollectionId;

	/** Stable identities of the bones created by the fracture execution. */
	UPROPERTY()
	TArray<FGuid> BoneIds;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Shared provider base for every fracture authoring node.
 *
 * Owns the Priority shared by every fracture-operation authoring node. Result selection belongs to the
 * GC | Fracture executor because only that node can observe which bones actually exist after the operation.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGUtilsFractureProviderSettings
	: public UPCGUtilsGeometryCollectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** Execution order when several operations share one Fracture pin. Lower runs first; ties keep wiring order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual FName GetMainOutputPin() const override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;

	/**
	 * Builds the operation this node describes. Derived nodes override this instead of CreateFactory, which
	 * owns the shared Priority plumbing and must not be bypassed.
	 */
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
		PURE_VIRTUAL(UPCGUtilsFractureProviderSettings::CreateFractureFactory, return nullptr;);

};
