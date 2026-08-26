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

	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const;

	/** Creates and context-binds a runtime operation. Common initialization cannot be bypassed by subclasses. */
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const;
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
	/** Evaluates one factory across its requested domain and materializes the matching mesh elements. */
	PCGUTILSDYNMESH_API bool EvaluateFactory(
		const UPCGUtilsDynMeshSelectionFactoryData* Factory,
		const FPCGUtilsDynMeshSelectionEvaluationContext& EvaluationContext,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection);
}
