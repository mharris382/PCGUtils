// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "Data/Registry/PCGDataType.h"

#include "PCGUtilsDynMeshFactoryData.generated.h"

struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="DynMesh Factory"))
struct FPCGUtilsDynMeshFactoryDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

/**
 * Base UObject transported through PCG pins. Factory instances are immutable after their provider emits them;
 * mutable, mesh-specific state belongs on the non-UObject operation created by a derived factory.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Factories")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshFactoryData : public UPCGData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshFactoryDataTypeInfo)

	UPROPERTY()
	int32 Priority = 0;

	virtual bool Prepare(FPCGContext* InContext) { return true; }
	virtual void AddDataDependency(const UPCGData* InData);
	virtual bool CanBeSerialized() const override { return false; }

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SupportsFullDataCrc() const override { return true; }

	UPROPERTY()
	TSet<TObjectPtr<UPCGData>> DataDependencies;
};
