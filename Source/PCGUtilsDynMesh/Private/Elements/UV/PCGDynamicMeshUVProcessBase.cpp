#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"

#include "Algo/Unique.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshUVProcessBase"

bool UPCGDynamicMeshUVProcessBaseSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

bool FPCGUtilsDynMeshUVProcessOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	if (!MeshData)
	{
		return false;
	}

	// UVs are an overlay: vertices and triangles are untouched, so a selection remains valid.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	if (UVLayer < 0)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NegativeUVLayer", "Dynamic Mesh UV operation requires a non-negative UV Layer index; the mesh was left unchanged."),
			Invocation.Context);
		return true;
	}

	UDynamicMesh* DynamicMesh = MeshData->GetMutableDynamicMesh();
	UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh)
	{
		return false;
	}

	TArray<int32> TriangleIDs;
	if (Invocation.SelectionData)
	{
		FGeometryScriptMeshSelection Selection;
		Selection.SetSelection(Invocation.SelectionData->GetSelection());
		Selection.ConvertToMeshIndexArray(*Mesh, TriangleIDs, EGeometryScriptIndexType::Triangle);
	}
	else
	{
		TriangleIDs.Reserve(Mesh->TriangleCount());
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			TriangleIDs.Add(TriangleID);
		}
	}

	TriangleIDs.Sort();
	TriangleIDs.SetNum(Algo::Unique(TriangleIDs));

	int32 InvalidTriangleCount = 0;
	for (int32 Index = TriangleIDs.Num() - 1; Index >= 0; --Index)
	{
		if (!Mesh->IsTriangle(TriangleIDs[Index]))
		{
			TriangleIDs.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			++InvalidTriangleCount;
		}
	}
	TriangleIDs.Sort();

	if (InvalidTriangleCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidTriangles", "Dynamic Mesh UV operation ignored {0} invalid or stale selected triangles."),
			FText::AsNumber(InvalidTriangleCount)), Invocation.Context);
	}

	if (TriangleIDs.IsEmpty() || !ShouldProcessUVs(Invocation, TriangleIDs))
	{
		return true;
	}

	if (!Mesh->HasAttributes())
	{
		Mesh->EnableAttributes();
	}

	UE::Geometry::FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
	check(Attributes);
	if (Attributes->NumUVLayers() <= UVLayer)
	{
		Attributes->SetNumUVLayers(UVLayer + 1);
	}

	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Attributes->GetUVLayer(UVLayer);
	if (!UVOverlay)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingUVOverlay", "Dynamic Mesh UV operation could not resolve the requested UV overlay."),
			Invocation.Context);
		return false;
	}

	return ProcessUVs(Invocation, *Mesh, *UVOverlay, TriangleIDs);
}

#undef LOCTEXT_NAMESPACE
