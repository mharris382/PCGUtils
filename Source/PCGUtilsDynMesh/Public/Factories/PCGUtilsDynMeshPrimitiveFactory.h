// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshOperation.h"

#include "PCGUtilsDynMeshPrimitiveFactory.generated.h"

class UDynamicMesh;

namespace PCGUtilsDynMeshPrimitiveFactoryConstants
{
	inline const FName OutputPin = TEXT("Builder");
	inline const FName BuildersInputPin = TEXT("Builders");
}

USTRUCT(meta=(PCG_DataTypeDisplayName="Primitive Builder"))
struct FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo : public FPCGUtilsDynMeshFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

class FPCGUtilsDynMeshPrimitiveOperation;

/** Immutable primitive-building configuration transported through PCG pins. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshPrimitiveFactoryData : public UPCGUtilsDynMeshFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo)

	/** Creates and context-binds a runtime operation. Common initialization cannot be bypassed by subclasses. */
	TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> CreateOperationInternal() const;
};

/**
 * Runtime, per-seed mesh-building step. Evaluated once a seed's world/local-space transform and bounds are
 * known; builds and returns a private mesh already placed in the seed's local space, so callers (including a
 * future decorator that combines several of these) never need to know whether the result came from a single
 * primitive or a composed sub-tree.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshPrimitiveOperation : public FPCGUtilsDynMeshOperation
{
public:
	/** Ownership of the returned mesh passes to the caller. */
	virtual UDynamicMesh* BuildMesh(const FTransform& SeedTransform, const FBox& SeedLocalBounds) const = 0;
};

namespace PCGUtilsDynMeshFactories
{
	PCGUTILSDYNMESH_API const TSet<FPCGDataTypeBaseId>& GetPrimitiveBuilderFactoryTypes();
}
