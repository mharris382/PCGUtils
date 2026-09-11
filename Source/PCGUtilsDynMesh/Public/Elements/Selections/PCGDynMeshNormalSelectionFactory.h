// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGDynMeshNormalSelectionFactory.generated.h"

/** Tests face or vertex normals directly and adapts face results for edge consumers. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshNormalSelectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector ReferenceDirection = FVector::UpVector;

	UPROPERTY()
	float DotThreshold = 0.9f;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeForDomainInternal(
		const FPCGUtilsDynMeshSelectionDomain& RequestedDomain) const override
	{
		return RequestedDomain.ElementType == UE::Geometry::EGeometryElementType::Vertex
			? UE::Geometry::EGeometryElementType::Vertex
			: UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Normal Direction Selection Selector DynMesh Select"))
class PCGUTILSDYNMESH_API UPCGDynMeshNormalSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshDomainSelectionSourceSettings
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

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
