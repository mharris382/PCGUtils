// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionComparison.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshVertexColorSelectionFactory.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshVertexColorSelectionMode : uint8
{
	Channel UMETA(DisplayName="Color Channel"),
	ColorDistance UMETA(DisplayName="Linear Color Distance")
};

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshVertexColorChannel : uint8
{
	Red UMETA(DisplayName="R"),
	Green UMETA(DisplayName="G"),
	Blue UMETA(DisplayName="B"),
	Alpha UMETA(DisplayName="A")
};

/** Selection factory that evaluates averaged Dynamic Mesh vertex colors. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshVertexColorSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGUtilsDynMeshVertexColorSelectionMode SelectionMode = EPCGUtilsDynMeshVertexColorSelectionMode::Channel;

	UPROPERTY()
	EPCGUtilsDynMeshVertexColorChannel Channel = EPCGUtilsDynMeshVertexColorChannel::Red;

	UPROPERTY()
	EPCGUtilsDynMeshDistanceComparison Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrGreater;

	UPROPERTY()
	double ChannelValue = 0.5;

	UPROPERTY()
	double Tolerance = 0.001;

	UPROPERTY()
	FLinearColor ReferenceColor = FLinearColor::White;

	UPROPERTY()
	double ColorDistanceThreshold = 0.1;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshVertexColorSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshVertexColorSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshVertexColorSelectionMode SelectionMode = EPCGUtilsDynMeshVertexColorSelectionMode::Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::Channel", EditConditionHides))
	EPCGUtilsDynMeshVertexColorChannel Channel = EPCGUtilsDynMeshVertexColorChannel::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::Channel", EditConditionHides))
	EPCGUtilsDynMeshDistanceComparison Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrGreater;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, UIMin="0.0", UIMax="1.0",
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::Channel", EditConditionHides))
	double ChannelValue = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0",
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::Channel && (Comparison==EPCGUtilsDynMeshDistanceComparison::NearlyEqual || Comparison==EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual)", EditConditionHides))
	double Tolerance = 0.001;

	/** RGBA reference color used by Linear Color Distance mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::ColorDistance", EditConditionHides))
	FLinearColor ReferenceColor = FLinearColor::White;

	/** Maximum four-channel Euclidean distance from Reference Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0", UIMax="2.0",
		EditCondition="SelectionMode==EPCGUtilsDynMeshVertexColorSelectionMode::ColorDistance", EditConditionHides))
	double ColorDistanceThreshold = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
