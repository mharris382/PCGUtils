// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGGeometryCollectionSelectSurface.generated.h"

UENUM(BlueprintType)
enum class EPCGGeometryCollectionSurfaceClass : uint8
{
	Exterior,
	Interior
};

UENUM(BlueprintType)
enum class EPCGGeometryCollectionSurfaceMatch : uint8
{
	/** At least one face has the requested surface class. */
	AnyFace UMETA(DisplayName="Any Face"),

	/** Every face has the requested surface class. */
	AllFaces UMETA(DisplayName="All Faces")
};

/** Selects geometry-bearing bones by their exact GC interior/exterior face classification. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectSurfaceFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGGeometryCollectionSurfaceClass SurfaceClass = EPCGGeometryCollectionSurfaceClass::Exterior;

	UPROPERTY()
	EPCGGeometryCollectionSurfaceMatch Match = EPCGGeometryCollectionSurfaceMatch::AnyFace;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;
	virtual void ApplyInversion(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FDataflowTransformSelection& InOutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections",
	meta=(Keywords="Geometry Collection GC Exterior Interior Surface Faces Bones Pieces"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectSurfaceSettings
	: public UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectSurface"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionSurfaceClass SurfaceClass = EPCGGeometryCollectionSurfaceClass::Exterior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionSurfaceMatch Match = EPCGGeometryCollectionSurfaceMatch::AnyFace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
