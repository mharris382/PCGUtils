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
	Min UMETA(DisplayName="Darken (Min)"),
	Max UMETA(DisplayName="Lighten (Max)"),
	Mix UMETA(DisplayName="Mix (Normal)"),
	Screen UMETA(DisplayName="Screen")
};

namespace PCGUtilsPainters
{
	/** Channel-wise blending in linear value space. Alpha is a channel, not implicit opacity. */
	PCGUTILSPAINTER_API FPCGUtilsDynMeshPainterValue BlendValues(
		const FPCGUtilsDynMeshPainterValue& Base, const FPCGUtilsDynMeshPainterValue& Blend,
		EPCGUtilsDynMeshPainterMathOperation Operation, float Factor);
}

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

	UPROPERTY()
	float Factor = 1.0f;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData> Mask;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Blends Base (A) with Blend (B) without materializing intermediate PCG data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters", meta=(Keywords="Painter blend math color colour mix normal add subtract multiply screen darken lighten DynMesh"))
class PCGUTILSPAINTER_API UPCGDynMeshPainterMathProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPainterMath"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool ShouldShowCompactNodeTitle() const override { return true; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterMathOperation Operation = EPCGUtilsDynMeshPainterMathOperation::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	/** Interpolate from A to the channel-wise blend result. Alpha is blended like other channels, not used as opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable, ClampMin="0", ClampMax="1"))
	float Factor = 1.0f;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
