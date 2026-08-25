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

bool FPCGDynamicMeshUVProcessBaseElement::ProcessMesh(
	UPCGDynamicMeshData* MeshData,
	const UPCGDynamicMeshSelectionData* SelectionData,
	FPCGContext* Context) const
{
	check(MeshData && Context);
	const UPCGDynamicMeshUVProcessBaseSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProcessBaseSettings>();
	check(Settings);

	if (Settings->UVLayer < 0)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NegativeUVLayer", "Dynamic Mesh UV operation requires a non-negative UV Layer index; the mesh was left unchanged."),
			Context);
		return true;
	}

	UDynamicMesh* DynamicMesh = MeshData->GetMutableDynamicMesh();
	UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh)
	{
		return false;
	}

	TArray<int32> TriangleIDs;
	if (SelectionData)
	{
		FGeometryScriptMeshSelection Selection;
		Selection.SetSelection(SelectionData->GetSelection());
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
			FText::AsNumber(InvalidTriangleCount)), Context);
	}

	if (TriangleIDs.IsEmpty() || !ShouldProcessUVs(MeshData, SelectionData, TriangleIDs, Context))
	{
		return true;
	}

	if (!Mesh->HasAttributes())
	{
		Mesh->EnableAttributes();
	}

	UE::Geometry::FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
	check(Attributes);
	if (Attributes->NumUVLayers() <= Settings->UVLayer)
	{
		Attributes->SetNumUVLayers(Settings->UVLayer + 1);
	}

	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Attributes->GetUVLayer(Settings->UVLayer);
	if (!UVOverlay)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingUVOverlay", "Dynamic Mesh UV operation could not resolve the requested UV overlay."),
			Context);
		return false;
	}

	return ProcessUVs(MeshData, *Mesh, *UVOverlay, TriangleIDs, Context);
}

#undef LOCTEXT_NAMESPACE
