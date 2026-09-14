// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshMergeByDistance.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGMergeByDistance.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshMergeByDistance"

#if WITH_EDITOR
FText UPCGDynMeshMergeByDistanceSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Merge by Distance");
}

FText UPCGDynMeshMergeByDistanceSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Welds vertices within the mesh-local distance. A DynMesh Selection or Selector restricts which vertices "
		"may be merged; unselected vertices and the surrounding topology remain otherwise untouched.");
}
#endif

bool UPCGDynMeshMergeByDistanceSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Vertex;
	return true;
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation>
UPCGDynMeshMergeByDistanceSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshMergeByDistanceOperation> Operation =
		MakeShared<FPCGUtilsDynMeshMergeByDistanceOperation>();
	Operation->MergeDistance = MergeDistance;
	Operation->bAveragePosition = bAveragePosition;
	Operation->bAverageColors = bAverageColors;
	return Operation;
}

FPCGElementPtr UPCGDynMeshMergeByDistanceSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshMergeByDistanceElement>();
}

bool FPCGUtilsDynMeshMergeByDistanceOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	check(Invocation.MeshData);
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

	TSet<int32> CandidateVertices;
	const TSet<int32>* CandidateVerticesPtr = nullptr;
	if (Invocation.SelectionData)
	{
		for (const uint64 EncodedID : Invocation.SelectionData->GetSelection().Selection)
		{
			CandidateVertices.Add(static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID));
		}
		CandidateVerticesPtr = &CandidateVertices;
	}

	Invocation.MeshData->GetMutableDynamicMesh()->EditMesh(
		[this, CandidateVerticesPtr](UE::Geometry::FDynamicMesh3& Mesh)
		{
			GeomUtil_MergeByDistance(
				Mesh, MergeDistance, bAveragePosition, bAverageColors, CandidateVerticesPtr);
		});
	return true;
}

#undef LOCTEXT_NAMESPACE
