// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"

#include "PCGUtilsDynMeshDomainSelectionFactory.generated.h"

namespace PCGUtilsDynMeshSelectionDomains
{
	/**
	 * Maps a geometry element domain to the matching Geometry Script mesh index type
	 * (Vertex -> Vertex, Edge -> Edge, Face/any other -> Triangle).
	 */
	PCGUTILSDYNMESH_API EGeometryScriptIndexType ToScriptIndexType(
		UE::Geometry::EGeometryElementType ElementType);

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
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshDomainSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bAllowPartialInclusion = true;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const final;

	/** Canonical native domain used when a caller is not adapting for a particular consumer. */
	UE::Geometry::EGeometryElementType GetNativeElementType() const { return GetNativeElementTypeInternal(); }

	UE::Geometry::EGeometryElementType GetNativeElementType(
		const FPCGUtilsDynMeshSelectionDomain& RequestedDomain) const
	{
		return GetNativeElementTypeForDomainInternal(RequestedDomain);
	}
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperation(FPCGContext* InContext) const;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const PURE_VIRTUAL(
		UPCGUtilsDynMeshDomainSelectionFactoryData::GetNativeElementTypeInternal,
		return UE::Geometry::EGeometryElementType::Face;);
	/** Lets a selector preserve a directly supported consumer domain while adapting all other domains. */
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeForDomainInternal(
		const FPCGUtilsDynMeshSelectionDomain& RequestedDomain) const
	{
		return GetNativeElementTypeInternal();
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const PURE_VIRTUAL(
		UPCGUtilsDynMeshDomainSelectionFactoryData::CreateNativeOperationInternal,
		return nullptr;);

	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const final;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Provider counterpart that exposes the conversion policy shared by domain-specific factories. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** Complement the selector after its native result is converted into the consumer's domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bInvertSelection = false;

	/** Include a target element when any incident source element is selected. Disable for full-inclusion conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bAllowPartialInclusion = true;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
};

/** Unified query counterpart that adds inline Selection materialization to a native-domain Selector. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshDomainSelectionSourceSettings
	: public UPCGUtilsDynMeshSelectionSourceSettings
{
	GENERATED_BODY()

public:
	/** Include a target element when any incident source element is selected. Disable for full-inclusion conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bAllowPartialInclusion = true;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};
