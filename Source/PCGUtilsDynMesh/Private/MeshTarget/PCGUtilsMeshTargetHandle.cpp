#include "MeshTarget/PCGUtilsMeshTargetHandle.h"

#include "Data/PCGDynamicMeshData.h"
#include "Operations/MeshRegionOperator.h"

// Defined here (not defaulted in the header) because FMeshRegionOperator is only forward-declared there;
// TUniquePtr<FMeshRegionOperator>'s destructor/move-assignment need the complete type, which this .cpp has.
FPCGUtilsMeshTargetHandle::FPCGUtilsMeshTargetHandle() = default;
FPCGUtilsMeshTargetHandle::~FPCGUtilsMeshTargetHandle() = default;
FPCGUtilsMeshTargetHandle::FPCGUtilsMeshTargetHandle(FPCGUtilsMeshTargetHandle&&) = default;
FPCGUtilsMeshTargetHandle& FPCGUtilsMeshTargetHandle::operator=(FPCGUtilsMeshTargetHandle&&) = default;

int32 FPCGUtilsMeshTargetHandle::GetSourceVertexID(int32 TargetVertexID) const
{
	if (RegionOperator.IsValid())
	{
		return RegionOperator->Region.MapVertexToBaseMesh(TargetVertexID);
	}
	return TargetVertexID;
}

TArray<UMaterialInterface*> FPCGUtilsMeshTargetHandle::GetMaterials() const
{
	TArray<UMaterialInterface*> Result;
	if (SourceMeshData)
	{
		Result.Reserve(SourceMeshData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceMeshData->GetMaterials())
		{
			Result.Add(Material);
		}
	}
	return Result;
}
