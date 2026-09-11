// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "PCGGeometryCollectionSelectRandom.generated.h"

UENUM(BlueprintType)
enum class EPCGGeometryCollectionRandomSelectionMode : uint8
{
	Percentage,
	BoneCount UMETA(DisplayName="Bone Count")
};

UENUM(BlueprintType)
enum class EPCGGeometryCollectionRandomSelectionDomain : uint8
{
	Pieces,
	Clusters,
	AllBones UMETA(DisplayName="All Bones")
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Selections")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectRandomFactoryData
	: public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGGeometryCollectionRandomSelectionMode Mode = EPCGGeometryCollectionRandomSelectionMode::Percentage;
	UPROPERTY()
	EPCGGeometryCollectionRandomSelectionDomain Domain = EPCGGeometryCollectionRandomSelectionDomain::Pieces;
	UPROPERTY()
	int32 Percentage = 50;
	UPROPERTY()
	int32 BoneCount = 1;
	UPROPERTY()
	int32 RandomSeed = 42;

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
	meta=(Keywords="Geometry Collection GC Random Percentage Count Bones Pieces Clusters"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionSelectRandomSettings
	: public UPCGUtilsGeometryCollectionSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCSelectRandom"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionRandomSelectionMode Mode = EPCGGeometryCollectionRandomSelectionMode::Percentage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGGeometryCollectionRandomSelectionDomain Domain = EPCGGeometryCollectionRandomSelectionDomain::Pieces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0", ClampMax="100",
			EditCondition="Mode == EPCGGeometryCollectionRandomSelectionMode::Percentage", EditConditionHides))
	int32 Percentage = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0",
			EditCondition="Mode == EPCGGeometryCollectionRandomSelectionMode::BoneCount", EditConditionHides))
	int32 BoneCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	int32 RandomSeed = 42;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
