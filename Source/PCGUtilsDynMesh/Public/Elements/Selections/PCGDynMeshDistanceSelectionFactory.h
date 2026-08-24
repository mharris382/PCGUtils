// Copyright Max Harris
// Distance filter concepts adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshDistanceSelectionFactory.generated.h"

class UPCGBasePointData;

namespace PCGDynMeshDistanceSelectionFactoryConstants
{
	inline const FName TargetsInputPin = TEXT("Targets");
}

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshDistanceComparison : uint8
{
	StrictlyEqual UMETA(DisplayName="Equal"),
	StrictlyNotEqual UMETA(DisplayName="Not Equal"),
	EqualOrGreater UMETA(DisplayName="Greater or Equal"),
	EqualOrSmaller UMETA(DisplayName="Less or Equal"),
	StrictlyGreater UMETA(DisplayName="Greater"),
	StrictlySmaller UMETA(DisplayName="Less"),
	NearlyEqual UMETA(DisplayName="Nearly Equal"),
	NearlyNotEqual UMETA(DisplayName="Nearly Not Equal")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshTargetDistanceMode : uint8
{
	Center UMETA(DisplayName="Center"),
	SphereBounds UMETA(DisplayName="Sphere Bounds"),
	BoxBounds UMETA(DisplayName="Box Bounds")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshDistanceMetric : uint8
{
	Euclidean,
	Manhattan,
	Chebyshev
};

/** Selection factory comparing each mesh element's distance to its nearest target PCG point. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshDistanceSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> TargetPointData;

	UPROPERTY()
	EPCGUtilsDynMeshDistanceComparison Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrSmaller;

	UPROPERTY()
	double DistanceThreshold = 100.0;

	UPROPERTY()
	double Tolerance = 0.001;

	UPROPERTY()
	EPCGUtilsDynMeshTargetDistanceMode TargetDistanceMode = EPCGUtilsDynMeshTargetDistanceMode::Center;

	UPROPERTY()
	EPCGUtilsDynMeshDistanceMetric DistanceMetric = EPCGUtilsDynMeshDistanceMetric::Euclidean;

	UPROPERTY()
	bool bOverlapIsZero = true;

	UPROPERTY()
	bool bConvertTargetsToLocalSpace = true;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshDistanceSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshDistanceSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshDistanceComparison Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrSmaller;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	double DistanceThreshold = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0",
		EditCondition="Comparison==EPCGUtilsDynMeshDistanceComparison::NearlyEqual || Comparison==EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual", EditConditionHides, Units="cm"))
	double Tolerance = 0.001;

	/** Which part of each target point is spatialized when measuring distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshTargetDistanceMode TargetDistanceMode = EPCGUtilsDynMeshTargetDistanceMode::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshDistanceMetric DistanceMetric = EPCGUtilsDynMeshDistanceMetric::Euclidean;

	/** When using target bounds, positions inside the target volume have zero distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="TargetDistanceMode!=EPCGUtilsDynMeshTargetDistanceMode::Center", EditConditionHides))
	bool bOverlapIsZero = true;

	/** Convert world-space target points into the target actor's local mesh space before evaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bConvertTargetsToLocalSpace = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
