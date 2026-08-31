// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsGCSelectionFactory.h"

#include "PCGGCSelectionFromPoints.generated.h"

class UPCGBasePointData;

namespace PCGGCSelectionFromPointsConstants
{
	inline const FName PointsInputPin = TEXT("Points");
}

/** Turns bone indices carried on PCG points into a native Geometry Collection bone selection. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGCSelectionFromPointsFactoryData : public UPCGUtilsGCSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> PointData;

	UPROPERTY()
	FName BoneIndexAttribute;

	UPROPERTY()
	bool bValidateSourceIdentity = true;

	virtual bool Evaluate(
		const FPCGUtilsGCSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Authors a reusable bone selection from points, so all the spatial logic can stay in ordinary PCG/PCGEx
 * filters upstream. This node does the bookkeeping those filters cannot: proving the bone indices still refer
 * to the collection state they were read from.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Selection Bone Indices From Points"))
class PCGUTILSFRACTURE_API UPCGGCSelectionFromPointsSettings : public UPCGUtilsGCFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectionFromPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Integer point attribute holding Geometry Collection bone indices, as written by GC Bones To Points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FName BoneIndexAttribute = PCGUtilsGCIdentity::BoneIndexAttribute;

	/**
	 * Require the points' recorded source collection state to match the collection being selected against.
	 *
	 * Leave this on. Bone indices are only meaningful against one exact collection state - fracture and prune
	 * both reindex them - so applying stale indices does not fail, it silently selects the wrong pieces.
	 * Disable only when deliberately re-applying indices to a collection you know is structurally identical.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bValidateSourceIdentity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGCFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
