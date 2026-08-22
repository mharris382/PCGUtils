// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshFactoryData.h"

#include "Serialization/ArchiveCrc32.h"

PCG_DEFINE_TYPE_INFO(FPCGUtilsDynMeshFactoryDataTypeInfo, UPCGUtilsDynMeshFactoryData)

void UPCGUtilsDynMeshFactoryData::AddDataDependency(const UPCGData* InData)
{
	if (InData)
	{
		DataDependencies.Add(const_cast<UPCGData*>(InData));
	}
}

void UPCGUtilsDynMeshFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		AddUIDToCrc(Ar);
		return;
	}

	int32 PriorityValue = Priority;
	Ar << PriorityValue;

	TArray<uint32> DependencyCrcs;
	DependencyCrcs.Reserve(DataDependencies.Num());
	for (const UPCGData* Dependency : DataDependencies)
	{
		DependencyCrcs.Add(Dependency ? Dependency->GetOrComputeCrc(true).GetValue() : 0);
	}
	DependencyCrcs.Sort();
	Ar << DependencyCrcs;
}
