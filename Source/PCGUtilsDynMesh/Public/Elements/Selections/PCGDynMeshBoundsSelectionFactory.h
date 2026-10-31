// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynMeshBoundsSelectionTypes.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshDotComparison.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGDynMeshBoundsSelectionFactory.generated.h"

class UPCGBasePointData;

namespace PCGDynMeshBoundsSelectionFactoryConstants
{
	inline const FName PointsInputPin = TEXT("Points");
}

/** Selection factory that tests mesh elements against the oriented bounds of one or more PCG points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshBoundsSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> PointData;

	UPROPERTY()
	EPCGDynMeshBoundsTestMode BoundsTestMode = EPCGDynMeshBoundsTestMode::ElementCenterInside;

	UPROPERTY()
	bool bConvertPointsToLocalSpace = true;

	UPROPERTY()
	bool bCompareNormal = false;

	UPROPERTY()
	FPCGAttributePropertyInputSelector NormalDirectionAttribute;

	UPROPERTY()
	FPCGUtilsDynMeshDotComparison NormalComparison;

	UPROPERTY()
	EPCGDynMeshBoundsNormalOverlapMode NormalOverlapMode = EPCGDynMeshBoundsNormalOverlapMode::Any;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Bounds Point Bounds Selection Selector DynMesh"))
class PCGUTILSDYNMESH_API UPCGDynMeshBoundsSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshSelectionSourceSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshBoundsSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** How edge and triangle vertices are tested against each oriented point bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshBoundsTestMode BoundsTestMode = EPCGDynMeshBoundsTestMode::ElementCenterInside;

	/** Convert world-space point bounds into the target actor's local mesh space before evaluating them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bConvertPointsToLocalSpace = true;

	/**
	 * Also require an element's normal to agree with a direction given by the point whose bounds it lies in.
	 * Each point supplies its own direction, so one node can select the faces facing one way under one point
	 * and the faces facing another way under the next.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Normal", meta=(PCG_Overridable))
	bool bCompareNormal = false;

	/**
	 * Attribute or property of the Points input giving the direction an element's normal is compared with.
	 * A vector in world space, or anything that converts to one; it need not be normalized but must not be zero.
	 * Defaults to the point's forward axis ($Rotation.Forward). Required when Compare Normal is on, unused
	 * otherwise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Normal",
		meta=(PCG_Overridable, EditCondition="bCompareNormal", EditConditionHides))
	FPCGAttributePropertyInputSelector NormalDirectionAttribute;

	/**
	 * How closely an element's normal must agree with its point's direction. By default the dot product of the
	 * two must be at least 0.9. A threshold attribute is read from the Points input, one value per point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Normal",
		meta=(PCG_Overridable, EditCondition="bCompareNormal", EditConditionHides))
	FPCGUtilsDynMeshDotComparison NormalComparison;

	/** Whether an element lying in several points' bounds must pass the normal comparison for any or for all of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Normal",
		meta=(PCG_Overridable, EditCondition="bCompareNormal", EditConditionHides))
	EPCGDynMeshBoundsNormalOverlapMode NormalOverlapMode = EPCGDynMeshBoundsNormalOverlapMode::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	UPCGDynMeshBoundsSelectionFactoryProviderSettings();

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
};
