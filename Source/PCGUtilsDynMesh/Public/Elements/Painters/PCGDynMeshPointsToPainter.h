// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGDynMeshPointsToPainter.generated.h"

class UPCGBasePointData;

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPointsToPainterMode : uint8
{
	Scalar,
	Color
};

/** Painter backed by ordered point datasets paired one-to-one with consuming DynMesh inputs. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPointsToPainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> PointDataSets;

	UPROPERTY()
	EPCGUtilsDynMeshPointsToPainterMode Mode = EPCGUtilsDynMeshPointsToPainterMode::Scalar;

	UPROPERTY()
	FPCGAttributePropertyInputSelector ValueSelector;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Converts vertex-index-aligned PCG point datasets into a scalar or color Painter. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPointsToPainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPointsToPainterProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPointsToPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPointsToPainterMode Mode = EPCGUtilsDynMeshPointsToPainterMode::Scalar;

	/** Scalar source, shown in Scalar mode. Defaults to $Density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshPointsToPainterMode::Scalar", EditConditionHides))
	FPCGAttributePropertyInputSelector ScalarValueSelector;

	/** Color source, shown in Color mode. Defaults to $Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGUtilsDynMeshPointsToPainterMode::Color", EditConditionHides))
	FPCGAttributePropertyInputSelector ColorValueSelector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
