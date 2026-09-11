// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshOperation.h"
#include "Selections/GeometrySelection.h"

#include "PCGUtilsDynMeshSelectionFactory.generated.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;

namespace PCGUtilsDynMeshSelectionFactoryConstants
{
	inline const FName OutputPin = TEXT("Selector");
	inline const FName FactoriesInputPin = TEXT("Selectors");
}

/** The homogeneous element/topology domain in which a selector tree is evaluated. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionDomain
{
	UE::Geometry::EGeometryElementType ElementType = UE::Geometry::EGeometryElementType::Face;
	UE::Geometry::EGeometryTopologyType TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
};

/** Read-only mesh state shared by every operation in one selection-factory evaluation. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionEvaluationContext
{
	FPCGUtilsDynMeshSelectionEvaluationContext(
		const UPCGDynamicMeshData* InMeshData,
		const UE::Geometry::FDynamicMesh3& InMesh,
		const FPCGUtilsDynMeshSelectionDomain& InDomain)
		: MeshData(InMeshData), Mesh(InMesh), Domain(InDomain)
	{
	}

	const UPCGDynamicMeshData* MeshData = nullptr;
	const UE::Geometry::FDynamicMesh3& Mesh;
	FPCGUtilsDynMeshSelectionDomain Domain;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="DynMesh Selector"))
struct FPCGUtilsDynMeshSelectionFactoryDataTypeInfo : public FPCGUtilsDynMeshFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

class FPCGUtilsDynMeshSelectionOperation;

/** Immutable selection predicate/configuration data transported through PCG pins. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selection")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshSelectionFactoryData : public UPCGUtilsDynMeshFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo)

	/** Complement this selector after evaluating it in the consumer's requested domain. */
	UPROPERTY()
	bool bInvertSelection = false;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const;

	/** Creates and context-binds a runtime operation. Common initialization cannot be bypassed by subclasses. */
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Internal representation bridge that exposes an existing mesh-bound Selection as a deferred Selector.
 * Selection modifier nodes use this as their child so their Selection and Selector modes execute the same
 * decorator factory implementation.
 */
UCLASS()
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshLiteralSelectionFactoryData
	: public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGDynamicMeshSelectionData> SelectionData;

	UPROPERTY()
	bool bAllowPartialInclusion = true;

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Runtime boolean predicate evaluated once per mesh element in the selected domain. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshSelectionOperation : public FPCGUtilsDynMeshOperation
{
public:
	virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext);
	virtual bool TestElement(int32 ElementID) const = 0;

protected:
	const FPCGUtilsDynMeshSelectionEvaluationContext* SelectionContext = nullptr;
};

namespace PCGUtilsDynMeshFactories
{
	PCGUTILSDYNMESH_API const TSet<FPCGDataTypeBaseId>& GetSelectionFactoryTypes();
}

namespace PCGUtilsDynMeshSelectionFactories
{
	/** Orders selectors by short-circuit precedence. Equal priorities retain their connection order. */
	PCGUTILSDYNMESH_API void SortByPriority(
		TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>>& Factories);

	/** Evaluates one factory across its requested domain and materializes the matching mesh elements. */
	PCGUTILSDYNMESH_API bool EvaluateFactory(
		const UPCGUtilsDynMeshSelectionFactoryData* Factory,
		const FPCGUtilsDynMeshSelectionEvaluationContext& EvaluationContext,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection);
}
