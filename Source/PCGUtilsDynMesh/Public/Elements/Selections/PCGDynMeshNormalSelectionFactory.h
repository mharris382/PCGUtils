// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGDynMeshNormalSelectionFactory.generated.h"

/** Selection factory that tests triangle or vertex normals, according to the final Build node's domain. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshNormalSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector ReferenceDirection = FVector::UpVector;

	UPROPERTY()
	float DotThreshold = 0.9f;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshNormalSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshNormalSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Reference direction used for both triangle and vertex evaluation domains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FVector ReferenceDirection = FVector::UpVector;

	/** Minimum Dot(ElementNormal, ReferenceDirection) for an element to pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="-1.0", ClampMax="1.0"))
	float DotThreshold = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
