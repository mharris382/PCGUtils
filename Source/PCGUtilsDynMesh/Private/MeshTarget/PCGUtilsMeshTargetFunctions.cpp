#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Operations/MeshRegionOperator.h"
#include "PCGContext.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMeshTargetFunctions"

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateFullMeshTarget(
	const UPCGDynamicMeshData* SourceData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context)
{
	using namespace UE::Geometry;

	FPCGUtilsMeshTargetHandle Handle;
	Handle.ApplyMethod = ApplyMethod;
	Handle.SourceType = EPCGUtilsMeshTargetSourceType::FullMesh;
	Handle.SourceMeshData = SourceData;
	Handle.Context = Context;

	const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
	const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Mesh Target: skipped an invalid Dynamic Mesh input."), Context);
		return Handle;
	}
	if (SourceMesh->TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh", "Mesh Target: skipped an empty Dynamic Mesh input."), Context);
		return Handle;
	}

	UDynamicMesh* WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
	WorkingMesh->SetMesh(*SourceMesh);

	Handle.TargetMesh = WorkingMesh;
	Handle.BaseMesh = WorkingMesh;
	Handle.bIsValid = true;
	return Handle;
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateSelectionTarget(
	const UPCGDynamicMeshSelectionData* SelectionData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context)
{
	using namespace UE::Geometry;

	FPCGUtilsMeshTargetHandle Handle;
	Handle.ApplyMethod = ApplyMethod;
	Handle.SourceType = EPCGUtilsMeshTargetSourceType::Selection;
	Handle.Context = Context;

	const UPCGDynamicMeshData* SourceData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
	Handle.SourceMeshData = SourceData;
	const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
	const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelection", "Mesh Target: skipped Mesh Selection data with no valid source mesh."), Context);
		return Handle;
	}
	if (SourceMesh->TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("EmptySourceMesh", "Mesh Target: skipped a Mesh Selection whose source mesh is empty."), Context);
		return Handle;
	}

	UDynamicMesh* WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
	WorkingMesh->SetMesh(*SourceMesh);
	Handle.BaseMesh = WorkingMesh;

	FGeometryScriptMeshSelection Selection;
	Selection.SetSelection(SelectionData->GetSelection());

	if (ApplyMethod == EPCGUtilsMeshSelectionApplyMethod::RegionReinsert)
	{
		TArray<int32> SelectedTriangles;
		Selection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);

		if (SelectedTriangles.IsEmpty())
		{
			// Legitimate no-op (eg an upstream filter found nothing this run): the target is trivially safe to
			// operate on, and Restore leaves BaseMesh (the unchanged working copy) as the final result.
			Handle.bIsEmptySelectionNoOp = true;
			Handle.TargetMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
			Handle.bIsValid = true;
			return Handle;
		}

		// Extracts the selected triangles into a standalone submesh (preserving normals/UVs/colors/material
		// IDs/PolyGroups via FDynamicMeshEditor::AppendTriangles) and records the boundary correspondence
		// between the selected and unselected mesh - the engine's purpose-built mechanism for this workflow.
		Handle.RegionOperator = MakeUnique<FMeshRegionOperator>(WorkingMesh->GetMeshPtr(), SelectedTriangles);

		UDynamicMesh* SubmeshWrapper = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		SubmeshWrapper->SetMesh(MoveTemp(Handle.RegionOperator->Region.GetSubmesh()));
		Handle.TargetMesh = SubmeshWrapper;
		Handle.bIsValid = true;
		return Handle;
	}
	else // SelectedVertexPositions
	{
		TArray<int32> SelectedVertices;
		Selection.ConvertToMeshIndexArray(*SourceMesh, SelectedVertices, EGeometryScriptIndexType::Vertex);

		if (SelectedVertices.IsEmpty())
		{
			Handle.bIsEmptySelectionNoOp = true;
			Handle.TargetMesh = WorkingMesh;
			Handle.bIsValid = true;
			return Handle;
		}

		Handle.SelectedVertexIDs = MoveTemp(SelectedVertices);

		// A complete, separate, vertex-ID-preserving copy: FDynamicMesh3's copy (via UDynamicMesh::SetMesh)
		// clones the internal per-element arrays verbatim, with no compaction/renumbering, so vertex N in the
		// source is vertex N here too - required for the position-only copy-back in
		// RestoreSelectedVertexPositions to target the correct vertices.
		UDynamicMesh* FullCopy = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		FullCopy->SetMesh(*SourceMesh);
		Handle.TargetMesh = FullCopy;
		Handle.bIsValid = true;
		return Handle;
	}
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateTarget(
	const UPCGData* InputData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context)
{
	if (const UPCGDynamicMeshData* FullMeshData = Cast<const UPCGDynamicMeshData>(InputData))
	{
		return CreateFullMeshTarget(FullMeshData, ApplyMethod, Context);
	}
	if (const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(InputData))
	{
		return CreateSelectionTarget(SelectionData, ApplyMethod, Context);
	}

	PCGLog::LogWarningOnGraph(LOCTEXT("UnsupportedDataType", "Mesh Target: input was neither Dynamic Mesh nor Dynamic Mesh Selection data."), Context);
	return FPCGUtilsMeshTargetHandle();
}

bool FPCGUtilsMeshTargetFunctions::RestoreRegion(FPCGUtilsMeshTargetHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!ensureMsgf(Handle.ApplyMethod == EPCGUtilsMeshSelectionApplyMethod::RegionReinsert,
		TEXT("RestoreRegion called on a handle created with a different apply method")))
	{
		return false;
	}

	if (!Handle.RegionOperator.IsValid())
	{
		// FullMesh source, or an empty-selection no-op: BaseMesh already holds the correct result.
		Handle.TargetMesh = Handle.BaseMesh;
		return true;
	}

	Handle.RegionOperator->Region.GetSubmesh() = MoveTemp(*Handle.TargetMesh->GetMeshPtr());
	const bool bSucceeded = Handle.RegionOperator->BackPropropagate();
	if (!bSucceeded)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ReinsertFailed",
			"Mesh Target: failed to weld the operated-on region back into the source mesh; some triangles may be missing from the result."),
			Handle.Context);
	}

	Handle.TargetMesh = Handle.BaseMesh;
	Handle.RegionOperator.Reset();
	return bSucceeded;
}

void FPCGUtilsMeshTargetFunctions::RestoreSelectedVertexPositions(FPCGUtilsMeshTargetHandle& Handle)
{
	using namespace UE::Geometry;

	if (!Handle.IsValid())
	{
		return;
	}

	if (!ensureMsgf(Handle.ApplyMethod == EPCGUtilsMeshSelectionApplyMethod::SelectedVertexPositions,
		TEXT("RestoreSelectedVertexPositions called on a handle created with a different apply method")))
	{
		return;
	}

	if (Handle.SourceType == EPCGUtilsMeshTargetSourceType::FullMesh || Handle.SelectedVertexIDs.IsEmpty())
	{
		// FullMesh source (TargetMesh already *is* BaseMesh), or an empty-selection no-op (nothing to copy):
		// BaseMesh already holds the correct result.
		Handle.TargetMesh = Handle.BaseMesh;
		return;
	}

	const FDynamicMesh3* OperatedMesh = Handle.TargetMesh->GetMeshPtr();
	const TArray<int32>& SelectedVertexIDs = Handle.SelectedVertexIDs;

	Handle.BaseMesh->EditMesh([OperatedMesh, &SelectedVertexIDs](FDynamicMesh3& M)
	{
		for (const int32 VertexID : SelectedVertexIDs)
		{
			if (M.IsVertex(VertexID) && OperatedMesh->IsVertex(VertexID))
			{
				M.SetVertex(VertexID, OperatedMesh->GetVertex(VertexID));
			}
		}
	});

	Handle.TargetMesh = Handle.BaseMesh;
}

FPCGPinProperties FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(
	FName Label, bool bAllowMultipleConnections, bool bAllowMultipleData)
{
	return FPCGPinProperties(Label,
		FPCGDataTypeIdentifier::Construct(EPCGDataType::DynamicMesh, UPCGDynamicMeshSelectionData::StaticClass()),
		bAllowMultipleConnections, bAllowMultipleData);
}

void FPCGUtilsMeshTargetFunctions::EmitOutput(
	FPCGContext* Context, const FPCGTaggedData& Input, const FPCGUtilsMeshTargetHandle& Handle, FName OutputPin)
{
	check(Handle.IsValid());

	UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
	OutputData->Initialize(Handle.GetTargetMesh(), /*bCanTakeOwnership=*/true, Handle.GetMaterials());

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
	Output.Data = OutputData;
	Output.Pin = OutputPin;
}

#undef LOCTEXT_NAMESPACE
