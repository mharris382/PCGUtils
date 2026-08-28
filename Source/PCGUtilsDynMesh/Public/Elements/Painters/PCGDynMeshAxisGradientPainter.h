// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGDynMeshAxisGradientPainter.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterCoordinateSpace : uint8
{
	Local UMETA(DisplayName="DynMesh Local"),
	World UMETA(DisplayName="World")
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshAxisGradientPainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector Axis = FVector::UpVector;

	UPROPERTY()
	float StartDistance = 0.0f;

	UPROPERTY()
	float EndDistance = 100.0f;

	UPROPERTY()
	bool bInvert = false;

	UPROPERTY()
	EPCGUtilsDynMeshPainterCoordinateSpace CoordinateSpace =
		EPCGUtilsDynMeshPainterCoordinateSpace::Local;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Creates a clamped linear scalar gradient projected along an axis. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshAxisGradientPainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshAxisGradientPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FVector Axis = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float StartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float EndDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterCoordinateSpace CoordinateSpace =
		EPCGUtilsDynMeshPainterCoordinateSpace::Local;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
