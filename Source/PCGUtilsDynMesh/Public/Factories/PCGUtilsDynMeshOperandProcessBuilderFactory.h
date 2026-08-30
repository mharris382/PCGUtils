// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Elements/PCGUtilsDynMeshProcessOperation.h"

#include "PCGUtilsDynMeshOperandProcessBuilderFactory.generated.h"

/** Binary Builder expression. Both children are evaluated for the same seed in the same coordinate space. */
UCLASS(ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Builders")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshOperandProcessBuilderFactoryData : public UPCGUtilsDynMeshBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData> PrimaryBuilder;

	UPROPERTY()
	TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData> OperandBuilder;

	/** Immutable settings snapshot; contains no hard UObject references or authoring context. */
	TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation;

	UPROPERTY()
	uint32 OperationConfigCrc = 0;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};
