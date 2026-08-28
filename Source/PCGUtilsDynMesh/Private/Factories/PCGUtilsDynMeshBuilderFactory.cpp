// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshBuilderFactory.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

PCG_DEFINE_TYPE_INFO(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo, UPCGUtilsDynMeshBuilderFactoryData)

bool FPCGUtilsDynMeshBuildResult::IsValid() const
{
	const UDynamicMesh* MeshObject = MeshData ? MeshData->GetDynamicMesh() : nullptr;
	return MeshObject != nullptr && MeshObject->GetMeshPtr() != nullptr;
}

const UPCGData* FPCGUtilsDynMeshBuildResult::GetProcessInputData() const
{
	return SelectionData ? static_cast<const UPCGData*>(SelectionData) : static_cast<const UPCGData*>(MeshData);
}

void FPCGUtilsDynMeshBuildResult::SetMeshData(UPCGDynamicMeshData* InMeshData)
{
	if (MeshData != InMeshData)
	{
		// The old selection referenced the mesh being replaced, so it cannot stay active.
		SelectionData = nullptr;
	}
	MeshData = InMeshData;
}

void FPCGUtilsDynMeshBuildResult::SetBuilderFrame(const FTransform& InFrame)
{
	// Rotation and location only - see the note on BuilderFrame about why a pivot carries no scale.
	BuilderFrame = FTransform(InFrame.GetRotation(), InFrame.GetLocation());
	bHasBuilderFrame = true;
}

void FPCGUtilsDynMeshBuildResult::MoveBuilderFrame(const FTransform& GeometryTransform)
{
	if (!bHasBuilderFrame)
	{
		return;
	}

	// Track where the frame's *origin* ends up, rather than composing the two transforms. This distinction
	// matters: a scale performed about the frame's own origin composes to a transform carrying a compensating
	// translation, which would drag the frame even though the pivot demonstrably did not move. Asking where
	// the origin landed gives zero motion in that case, and the expected motion for a translation.
	SetBuilderFrame(FTransform(
		GeometryTransform.GetRotation() * BuilderFrame.GetRotation(),
		GeometryTransform.TransformPosition(BuilderFrame.GetLocation())));
}

void FPCGUtilsDynMeshBuildResult::SetSelectionData(const UPCGDynamicMeshSelectionData* InSelectionData)
{
	checkf(!InSelectionData || InSelectionData->GetSourceMeshData() == MeshData,
		TEXT("A Builder result's active selection must reference that result's own mesh data."));
	SelectionData = InSelectionData;
}

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGUtilsDynMeshBuilderFactoryData::CreateOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Operation = CreateOperationInternal();
	if (!Operation)
	{
		return nullptr;
	}

	Operation->BindContext(InContext);
	if (!Operation->Prepare())
	{
		return nullptr;
	}
	return Operation;
}

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGUtilsDynMeshBuilderFactoryData::CreateOperationInternal() const
{
	return nullptr;
}

namespace PCGUtilsDynMeshFactories
{
	const TSet<FPCGDataTypeBaseId>& GetBuilderFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId()};
		return Types;
	}
}
