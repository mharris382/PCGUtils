// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshDotComparison.h"

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
	FPCGUtilsDynMeshDotComparison Comparison;

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

	/**
	 * How closely an element's normal must agree with Reference Direction to pass. By default the dot product of
	 * the two must be at least 0.9. A threshold attribute is read from the mesh the selector is evaluated on, so
	 * it must be a Data-domain attribute (@Data.Name).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FPCGUtilsDynMeshDotComparison Comparison;

	/** The threshold this node had before Comparison existed. Moved into Comparison when the node is loaded. */
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use Comparison."))
	float DotThreshold_DEPRECATED = UnsetLegacyDotThreshold;

	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;

private:
	/** Outside the range a dot product can take, so a saved legacy threshold can be told from none. */
	static constexpr float UnsetLegacyDotThreshold = -2.0f;
};
