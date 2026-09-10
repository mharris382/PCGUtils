// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshSetPolygroup.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshPolygroupFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSetPolygroup"

#if WITH_EDITOR
FText UPCGDynMeshSetPolygroupSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh|Set PolyGroup");
}

FText UPCGDynMeshSetPolygroupSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Assigns one PolyGroup ID to a Dynamic Mesh triangle selection. With no selection the whole mesh is used. "
		"The default PolyGroup layer always exists; an extended layer is created if missing.");
}
#endif

bool UPCGDynMeshSetPolygroupSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

FPCGElementPtr UPCGDynMeshSetPolygroupSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSetPolygroupElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshSetPolygroupSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshSetPolygroupOperation> Operation =
		MakeShared<FPCGUtilsDynMeshSetPolygroupOperation>();
	Operation->GroupLayer = GroupLayer;
	Operation->PolygroupID = PolygroupID;
	Operation->bGenerateNewPolygroup = bGenerateNewPolygroup;
	return Operation;
}

bool FPCGUtilsDynMeshSetPolygroupOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	UDynamicMesh* TargetMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
	if (!TargetMesh)
	{
		return false;
	}

	// Only the PolyGroup attribute changes; the triangulation is untouched.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	// SetPolygroupForMeshSelection does not create the target layer - ensure it exists first.
	if (GroupLayer.bDefaultLayer)
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::EnablePolygroups(TargetMesh);
	}
	else
	{
		const int32 RequiredLayers = FMath::Max(0, GroupLayer.ExtendedLayerIndex) + 1;
		TargetMesh->EditMesh([RequiredLayers](UE::Geometry::FDynamicMesh3& EditMesh)
		{
			if (!EditMesh.HasAttributes())
			{
				EditMesh.EnableAttributes();
			}
			if (EditMesh.Attributes()->NumPolygroupLayers() < RequiredLayers)
			{
				EditMesh.Attributes()->SetNumPolygroupLayers(RequiredLayers);
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, /*bDeferChangeNotifications=*/true);
	}

	FGeometryScriptMeshSelection Selection;
	if (Invocation.SelectionData)
	{
		Selection.SetSelection(Invocation.SelectionData->GetSelection());
	}
	else
	{
		UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(
			TargetMesh, Selection, EGeometryScriptMeshSelectionType::Triangles);
	}

	int32 SetPolygroupIDOut = 0;
	UGeometryScriptLibrary_MeshPolygroupFunctions::SetPolygroupForMeshSelection(
		TargetMesh, GroupLayer, Selection, SetPolygroupIDOut, PolygroupID, bGenerateNewPolygroup,
		/*bDeferChangeNotifications=*/false);
	return true;
}

#undef LOCTEXT_NAMESPACE
