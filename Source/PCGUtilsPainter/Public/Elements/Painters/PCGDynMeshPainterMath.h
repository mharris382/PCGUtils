// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGDynMeshPainterMath.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterMathOperation : uint8
{
	Add UMETA(DisplayName="Add"),
	Subtract UMETA(DisplayName="Subtract"),
	Multiply UMETA(DisplayName="Multiply"),
	Min UMETA(DisplayName="Min"),
	Max UMETA(DisplayName="Max")
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshPainterMathFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGUtilsDynMeshPainterMathOperation Operation = EPCGUtilsDynMeshPainterMathOperation::Multiply;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> A;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> B;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Composes two Painter fields without materializing intermediate PCG data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshPainterMathProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPainterMath"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool ShouldShowCompactNodeTitle() const override { return true; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterMathOperation Operation = EPCGUtilsDynMeshPainterMathOperation::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
