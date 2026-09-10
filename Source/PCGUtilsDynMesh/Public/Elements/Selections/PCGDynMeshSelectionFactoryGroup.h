// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshSelectionFactoryGroup.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionFactoryGroupMode : uint8
{
	And UMETA(DisplayName="AND"),
	Or UMETA(DisplayName="OR"),
	Not UMETA(DisplayName="NOT")
};

/** Composite factory holding direct UObject references to its child selection factories. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionFactoryGroupData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGUtilsDynMeshSelectionFactoryGroupMode Mode = EPCGUtilsDynMeshSelectionFactoryGroupMode::And;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> ChildFactories;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Intersect Add Union Invert Selection Logic Selector DynMesh Select AND OR NOT"))
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionFactoryGroupProviderSettings
	: public UPCGUtilsDynMeshSelectionSourceSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionFactoryGroup"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool ShouldShowCompactNodeTitle() const override { return true; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshSelectionFactoryGroupMode Mode = EPCGUtilsDynMeshSelectionFactoryGroupMode::And;

	/** Evaluation priority when this group is nested. Higher values are evaluated first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
};
