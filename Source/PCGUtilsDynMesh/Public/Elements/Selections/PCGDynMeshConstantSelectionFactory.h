// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGDynMeshConstantSelectionFactory.generated.h"

/** Domain-agnostic predicate that always returns the same fixed result. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshConstantSelectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bAlwaysPass = true;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeForDomainInternal(
		const FPCGUtilsDynMeshSelectionDomain& RequestedDomain) const override
	{
		// The result is the same in every domain, so evaluate directly in whatever domain is requested rather
		// than forcing an unnecessary conversion from a fixed native domain.
		return RequestedDomain.ElementType;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * A fixed DynMesh Selector that always selects every element, or none, in whatever domain it is evaluated.
 *
 * Useful as a neutral placeholder while building a selection graph, or as an explicit true/false leaf for
 * selection logic. Only the two named aliases appear in the context menu; there is no third, unconfigured
 * default.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections|Constants",
	meta=(Keywords="DynMesh Select Constant Always Pass Fail True False All None"))
class PCGUTILSDYNMESH_API UPCGDynMeshConstantSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshConstantSelectionFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bAlwaysPass = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
