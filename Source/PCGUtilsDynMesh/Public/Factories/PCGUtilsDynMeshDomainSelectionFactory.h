// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "PCGUtilsDynMeshDomainSelectionFactory.generated.h"

namespace PCGUtilsDynMeshSelectionDomains
{
	/** Converts a Triangle-topology selection to the requested vertex, edge, or face element domain. */
	PCGUTILSDYNMESH_API bool ConvertSelection(
		const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FGeometrySelection& FromSelection,
		UE::Geometry::EGeometryElementType ToElementType,
		bool bAllowPartialInclusion,
		UE::Geometry::FGeometrySelection& OutSelection);
}

/**
 * Factory base for predicates whose implementation has one native element domain.
 * The runtime wrapper evaluates the native predicate once and implicitly converts it when the consuming Build node
 * requests another vertex/edge/face domain.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshDomainSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bAllowPartialInclusion = true;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const final;

	UE::Geometry::EGeometryElementType GetNativeElementType() const { return GetNativeElementTypeInternal(); }
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperation(FPCGContext* InContext) const;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const PURE_VIRTUAL(
		UPCGUtilsDynMeshDomainSelectionFactoryData::GetNativeElementTypeInternal,
		return UE::Geometry::EGeometryElementType::Face;);
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const PURE_VIRTUAL(
		UPCGUtilsDynMeshDomainSelectionFactoryData::CreateNativeOperationInternal,
		return nullptr;);

	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const final;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Provider counterpart that exposes the conversion policy shared by domain-specific factories. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** Include a target element when any incident source element is selected. Disable for full-inclusion conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bAllowPartialInclusion = true;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};
