// Copyright Max Harris

#include "Target/PCGUtilsPainterDynMeshTarget.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"

FPCGUtilsPainterDynMeshTarget::FPCGUtilsPainterDynMeshTarget(
	UPCGDynamicMeshData* InMeshData,
	const FTransform& InLocalToWorld,
	int32 InDataSetIndex,
	int32 InDataSetCount)
	: MeshData(InMeshData)
	, LocalToWorld(InLocalToWorld)
	, DataSetIndex(InDataSetIndex)
	, DataSetCount(InDataSetCount)
{
	UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
	CanonicalMesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
}
