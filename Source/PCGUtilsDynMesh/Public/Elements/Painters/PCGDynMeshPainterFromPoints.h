// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGDynMeshPainterFromPoints.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterFalloff : uint8
{
	Hard UMETA(DisplayName="Hard"),
	Linear UMETA(DisplayName="Linear"),
	Smooth UMETA(DisplayName="Smooth")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterPointReduction : uint8
{
	Max UMETA(DisplayName="Max"),
	Min UMETA(DisplayName="Min"),
	Add UMETA(DisplayName="Add"),
	Multiply UMETA(DisplayName="Multiply")
};

USTRUCT()
struct FPCGUtilsDynMeshPreparedPaintPoint
{
	GENERATED_BODY()

	UPROPERTY()
	FVector WorldPosition = FVector::ZeroVector;

	UPROPERTY()
	float Value = 0.0f;
};

/** A scalar field made from prepared spherical brushes in PCG world space. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPainterFromPointsFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FPCGUtilsDynMeshPreparedPaintPoint> Points;

	UPROPERTY()
	float Radius = 100.0f;

	UPROPERTY()
	FPCGAttributePropertyInputSelector ValueSelector;

	UPROPERTY()
	EPCGUtilsDynMeshPainterFalloff Falloff = EPCGUtilsDynMeshPainterFalloff::Smooth;

	UPROPERTY()
	EPCGUtilsDynMeshPainterPointReduction Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;

	UPROPERTY()
	bool bClampResult = true;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Converts PCG points into reusable spherical scalar brushes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGDynMeshPainterFromPointsProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshPainterFromPointsProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPainterFromPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** World-space radius shared by all input point brushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable, ClampMin="0.0001"))
	float Radius = 100.0f;

	/** Scalar value read once per input point while the Painter is prepared. Defaults to $Density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector ValueSelector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterFalloff Falloff = EPCGUtilsDynMeshPainterFalloff::Smooth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterPointReduction Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	bool bClampResult = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
